#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "rpmss.h"
#include "set.c"

static unsigned *v;
static int bpp;

static int decode(const char *s)
{
    int Mshift;
    if (decode_set_init(s, &bpp, &Mshift) < 0)
	return -1;
    int len = strlen(s);
    int n = decode_set_size(len, Mshift);
    v = realloc(v, n * sizeof(unsigned));
    return decode_set(s, Mshift, v);
}

static char *ss;

static int setconv(const char *s)
{
    if (strncmp(s, "set:", 4) == 0)
	s += 4;
    int n = decode(s);
    if (n <= 0) return 1;
    int len = rpmssEncodeSize(v, n, bpp);
    if (n <= 0) return 1;
    ss = realloc(ss, len);
    if (rpmssEncode(v, n, bpp, ss) <= 0)
	return 1;
    printf("set:%s\n", ss);
    return 0;
}

int main(int argc, const char **argv)
{
    assert(argc == 1 || argc == 2);
    if (argc == 2)
	return setconv(argv[1]);
    int rc = 0;
    char *line = NULL;
    size_t alloc_size = 0;
    ssize_t len;
    while ((len = getline(&line, &alloc_size, stdin)) >= 0) {
	if (len > 0 && line[len-1] == '\n')
	    line[--len] = '\0';
	if (len == 0)
	    continue;
	rc |= setconv(line);
    }
    free(line);
    return rc;
}

/* ex: set ts=8 sts=4 sw=4 noet: */
