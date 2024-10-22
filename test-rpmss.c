#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <getopt.h>
#include "rpmss.h"
#include "qsort.h"

static
void test_set(unsigned *v0, int n0, int bpp0, int print)
{
    // encode
    int strsize = rpmssEncodeInit(v0, n0, bpp0);
    // too many values with too small bpp range
    if (strsize == -5)
	return;
    assert(strsize > 0);
    // check overruns
    char *sbuf = malloc(strsize + 1024);
    // trigger align
    char *s = sbuf + 1;
    int len = rpmssEncode(v0, n0, bpp0, s);
    assert(len > 0);
    assert(len < strsize);
    assert(s[len] == '\0');
    if (print)
	printf("set:%s\n", s);
    // decode
    int bpp1;
    int v1size = rpmssDecodeInit(s, len, &bpp1);
    assert(v1size >= n0);
    unsigned *v1 = malloc(v1size * sizeof(unsigned));
    int n1 = rpmssDecode(s, v1);
    assert(n1 > 0);
#if 0
    rpmssDecode(s, len, v1, &bpp1);
    rpmssDecode(s, len, v1, &bpp1);
    rpmssDecode(s, len, v1, &bpp1);
    rpmssDecode(s, len, v1, &bpp1);
    rpmssDecode(s, len, v1, &bpp1);
    rpmssDecode(s, len, v1, &bpp1);
    rpmssDecode(s, len, v1, &bpp1);
#endif
    // compare
    assert(n0 == n1);
    int i;
    for (i = 0; i < n0; i++)
	assert(v0[i] == v1[i]);
    free(sbuf);
    free(v1);
}

static
void sortv(int c, unsigned *v)
{
#define LT(a, b) ((*a) < (*b))
    QSORT(unsigned, v, c, LT);
}

static
int uniqv(int c, unsigned *v)
{
    int i, j;
    for (i = 0, j = 0; i < c; i++) {
       while (i + 1 < c && v[i] == v[i + 1])
           i++;
       v[j++] = v[i];
    }
    assert(j <= c);
    return j;
}

static
int make_random_set(int c, unsigned **pv, int bpp)
{
    unsigned *v = *pv = malloc(c * sizeof(unsigned));
    int i;
    unsigned mask = ~0u;
    if (bpp < 32)
	mask = (1 << bpp) - 1;
    for (i = 0; i < c; i++) {
	v[i] = rand();
	if (bpp < 32)
	    v[i] &= mask;
	else
	    v[i] ^= (unsigned) rand() << 4;
    }
    sortv(c, v);
    c = uniqv(c, v);
    return c;
}

static
void test_random_set(int n0, int bpp, int print)
{
    unsigned *v;
    int n = make_random_set(n0, &v, bpp);
    assert(n > 0);
    assert(n <= n0);
    test_set(v, n, bpp, print);
    free(v);
}

static
int rand_range(int min, int max)
{
    assert(max >= min);
    return min + rand() % (max - min + 1);
}

int main(int argc, char **argv)
{
    int runs = 9999;
    int min_bpp = 7;
    int max_bpp = 32;
    int min_size = 1;
    int max_size = 99999;
    int print = 0;
    int opt;
    while ((opt = getopt(argc, argv, "n:b:B:s:S:p")) != -1)
	switch (opt) {
	case 'n':
	    runs = atoi(optarg);
	    break;
	case 'b':
	    min_bpp = atoi(optarg);
	    break;
	case 'B':
	    max_bpp = atoi(optarg);
	    break;
	case 's':
	    min_size = atoi(optarg);
	    break;
	case 'S':
	    max_size = atoi(optarg);
	    break;
	case 'p':
	    print = 1;
	    break;
	default:
	    assert(!"option");
	}
    int i;
    for (i = 0; i < runs; i++) {
	int bpp = rand_range(min_bpp, max_bpp);
	int size = rand_range(min_size, max_size);
	test_random_set(size, bpp, print);
    }
    return 0;
}

// ex: set ts=8 sts=4 sw=4 noet:
