/*
 * Copyright 2000-2010,2012 BitMover, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bkd.h"
#include "progress.h"

/*
 * Provide compress/uncompress streams using the zgets.c code.
 * This code need to be compatible with the previous version from bk-4.1
 * and earlier.
 *
 * This is a normal gzip stream of data split up into blocks so that
 * it can be read unbuffered without reading past the compressed
 * stream.  Each block starts with a 2-byte length count encoded in
 * network byte order followed by that number of bytes of data.  The
 * last block is just a 2-byte length of zero.
 *
 * This code has the following restrictions:
 *   - Each compressed block must be <= 32768 bytes including the header
 *
 * The data stream generated by this code must also be compatible with
 * bk-4.1 which has the following restrictions:
 *   - Each compressed block must be <= 4098 bytes (with header)
 *   - All data must be complete in the block before the block
 *     containing the gzip EOF marker and checksum.
 * (These two restrictions require zgets.c to use Z_PARTIAL_FLUSH and
 *  a blocksize of 4k)
 *
 * Neither version has a restriction on how much uncompressed data can
 * come from a single block.
 */

int
gzip_main(int ac, char **av)
{
	int c, rc, gzip_level = -2;

	while ((c = getopt(ac, av, "duz:", 0)) != -1) {
		switch (c) {
		    case 'd':
		    case 'u':	gzip_level = -1; break;
		    case 'z':	gzip_level = atoi(optarg); break;
		    default: bk_badArg(c, av);
		}
	}
	if (gzip_level >= 0) {
		rc = gzipAll2fh(0, stdout, gzip_level, 0, 0, 0);
	} else if (gzip_level == -1) {
		rc = gunzipAll2fh(0, stdout, 0, 0);
	} else {
		usage();
	}
	return (rc);
}

int
gzipAll2fh(int rfd, FILE *wf, int level, int *in, int *out, int verbose)
{
	int	n;
	FILE	*zout;
	ticker	*tick = 0;
	char	buf[8<<10];

	setmode(rfd, _O_BINARY);
	zout = fopen_zip(wf, "wh#", level, in, out);
	if (getenv("BK_NOTTY")) verbose = 0;
	/*
	 * must use readn() here, needed for consistant block sizes so
	 * recompressing the stream for http push won't change.
	 */
	// XXX verbose is never set
	if (verbose) tick = progress_start(PROGRESS_SPIN, 0);
	while ((n = readn(rfd, buf, sizeof(buf))) > 0) {
		fwrite(buf, 1, n, zout);
		if (tick) progress(tick, 0);
	}
	if (tick) progress_done(tick, 0);
	if (fclose(zout)) return (-1);
	return (0);
}

int
gunzipAll2fh(int rfd, FILE *wf, int *in, int *out)
{
	int	i, fd;
	int	moved = 0, offset = 0;
	FILE	*zin, *f;
	char	buf[8<<10];

	if (getenv("BK_REGRESSION")) {
		char	*p = getenv("BK_DIE_OFFSET");

		if (p) offset = atoi(p);
	}
	setmode(rfd, _O_BINARY);
	f = fdopen(dup(rfd), "rb");
	fflush(wf);
	fd = fileno(wf);
	if (in) *in = 0;
	if (out) *out = 0;
	zin = fopen_zip(f, "rhu#", in, out);
	while ((i = fread(buf, 1, sizeof(buf), zin)) > 0) {
		moved += i;
		if (offset && (moved >= offset)) exit(1);
		if (writen(fd, buf, i) != i) {
			fclose(zin);
			fclose(f);
			return (1);
		}
	}
	i = fclose(zin);
	fclose(f);
	return (i ? 1 : 0);
}
