/* glob.c Copyright (c) 2003 BitMover, Inc. */

#include "system.h"
#include "regex.h"	/* has to be second, conflicts w/ system .h's */
#define		C(c)	(ignorecase ? tolower(c) : c)
#ifdef	TEST
#undef	calloc
#undef strdup
#endif

/*
 * This file contains routines to match file names against lists of
 * shell glob patterns.  We support only ? * [...] as metacharacters.
 * \ escapes the following character.  You are expected to provide
 * only the filename, not its path (therefore there is no special
 * treatment of /).
 */

private int
glob_equals(void)
{
	static int doit = -1;

	if (doit == -1) {
		char	*s = getenv("BK_GLOB_EQUAL");

		doit = s && streq(s, "YES");
	}
	return (doit);
}

int
is_glob(char *glob)
{
	int	escape = 0;

	unless (glob && *glob) return (0);

	for ( ; *glob; glob++) {
		if (*glob == '\\') {
			escape = !escape;
			continue;
		}
		if (escape) continue;
		switch (*glob) {
		    case '=':	unless (glob_equals()) break;
		    case '?':
		    case '*':
		    case '[':	return (1);
		}
		escape = 0;
	}
	return (0);
}

char*
unescape(char *glob)
{
	char	*ret, *p;
	int	escape = 0;

	unless (glob && *glob) return (0);
	ret = strdup(glob);

	for (p = ret; *glob; glob++) {
		if (*glob == '\\') {
			if (escape) *p++ = '\\';
			escape = !escape;
		} else {
			*p++ = *glob;
		}
	}
	*p = 0;
	return (ret);
}

/* Match a string against one glob pattern.  */
int
match_one(char *string, char *glob, int ignorecase)
{
	u8 	*p, *g;
	int	invert, match;

	p = string;
	g = glob;
	while (*g) {
		switch (*g) {
		    case '?':
			if (*p) break;
			return (0);
		    case '[':
			g++;
			if (invert = (*g == '^')) g++;
			unless (strchr(g, ']')) {
				fprintf(stderr, "bad glob: %s\n", glob);
				return (0);
			}
			match = 0;
			do {
				// XXX - we don't make sure it is not [a-]
				// XXX - we don't support [a\-xyz]
				if (g[1] == '-') {
					if ((C(*g) <= C(*p)) &&
					    (C(*p) <= C(g[2]))) {
					    	match = 1;
					}
					g += 2;
				} else if (C(*g) == C(*p)) {
					match = 1;
				}
			} while (*++g != ']');
			if (match ^ invert) break;
			return (0);
		    case '=':
		    	unless (glob_equals()) {
				if (C(*p) == C(*g)) break;
				return (0);
			}
			/* fall through */
		    case '*':
			g++;
			unless (*g) return (1);
			goto star;
		    case '\\':
			g++;
		    default:
			if (C(*p) == C(*g)) break;
			return (0);
		}
		p++;
		g++;
	}
	return (*p == 0);

	/* Star in the middle of a pattern is handled by scanning
	 * ahead for the next place the pattern might match, then
	 * recursively calling match_one with the rest of the pattern.
	 * If the next char in the pattern is not a metacharacter, we
	 * can seek forward rapidly; otherwise we can't.
	 */
star:	while (*p) {
		unless (*g == '\\' || *g == '[' || *g == '?' || *g == '*') {
			while (*p && C(*p) != C(*g)) p++;
			unless (*p) return (0);
		}
		if (match_one(p, g, ignorecase)) return (1);
		p++;
	}
	return (0);
}

/* If the string matches any of the globs in the vector, return a pointer
 * to the first glob that matched.  Otherwise return NULL.
 */
char *
match_globs(char *string, char **globs, int ignorecase)
{
	int i;
	if (globs == NULL) return NULL;

	EACH (globs) {
		if (match_one(string, globs[i], ignorecase)) {
			return globs[i];
		}
	}
	return NULL;
}

search
search_parse(char *str)
{
	char	*p;
	search  s;

	bzero(&s, sizeof (s));
	p = strchr(str, '/');
	unless (p) {
		fprintf(stderr, "unterminated search pattern: /%s\n", str);
		exit(1);
	}
	*p++ = 0;
	while (*p) {
		switch (*p++) {
		    case 'i': s.ignorecase = 1; break;
		    case 'g': s.want_glob = 1; break;
		    case 'r': s.want_re = 1; break;
		    default:
			fprintf(stderr, "search: /%c not implemented\n", p[-1]);
			return (s);	/* pattern is null */
		}
	}
	if (s.want_glob && s.want_re) {
		fprintf(stderr, "search: /g or /r but not both\n");
		return (s);
	}
	str = s.pattern = strdup(str);
	if (s.want_glob) return (s);
	s.want_re = 1;
	if (s.ignorecase) for (p = str; *p = tolower(*p); p++);
	if (re_comp(str)) {
		fprintf(stderr, "search: bad regex \"%s\"\n", str);
		free(str);
		s.pattern = 0;
		return (s);
	}
	return (s);
}

int
search_either(char *str, search s)
{
	return (s.want_re ? search_regex(str, s) : search_glob(str, s));
}

int
search_glob(char *str, search s)
{
	char	*glob;
	int	ret;

	unless (s.pattern) return (0);
	glob = aprintf("*%s*", s.pattern);
	ret = match_one(str, glob, s.ignorecase);
	free(glob);
	return (ret);
}

int
search_regex(char *str, search s)
{
	unless (s.pattern && s.want_re) return (0);
	if (s.ignorecase) {
		char	*p;
		int	ret;

		str = strdup(str);
		for (p = str; *p = tolower(*p); p++);
		ret = re_exec(str);
		free(str);
		return (ret);
	}
	return (re_exec(str));
}

char	**
globdir(char *dir, char *glob)
{
	char	**list = getdir(dir);
	char	**matches = 0;
	int	i;

	unless (list) return (0);
	EACH(list) {
		if (match_one(list[i], glob, 0)) {
			matches = addLine(matches, list[i]);
			list[i] = (char*)1;
		}
	}
	EACH(list) {
		unless (list[i] == (char*)1) free(list[i]);
	}
	freeLines(list, 0);
	return (matches);
}