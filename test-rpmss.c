#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "rpmss.h"

static
void test_set(int c0, unsigned *v0, int bpp0)
{
    // encode
    int strsize = rpmssEncodeSize(c0, bpp0);
    assert(strsize > 0);
    char *s = malloc(strsize);
    int len = rpmssEncode(c0, v0, bpp0, s);
    assert(len > 0);
    assert(len < strsize);
    fprintf(stderr, "set:%s\n", s);
    assert(s[len] == '\0');
    // decode
    int v1size = rpmssDecodeSize(s, len);
    assert(v1size >= c0);
    unsigned *v1 = malloc(v1size * sizeof(unsigned));
    int bpp1;
    int c1 = rpmssDecode(s, v1, &bpp1);
    // compare
    fprintf(stderr, "c0=%d c1=%d\n", c0, c1);
    assert(c0 == c1);
    int i;
    for (i = 0; i < c0; i++)
	assert(v0[i] == v1[i]);
    free(s);
    free(v1);
}

static
int cmp(const void *arg1, const void *arg2)
{
    unsigned v1 = *(unsigned *) arg1;
    unsigned v2 = *(unsigned *) arg2;
    if (v1 > v2)
	return 1;
    if (v1 < v2)
	return -1;
    return 0;
}

static
void sortv(int c, unsigned *v)
{
    qsort(v, c, sizeof(unsigned), cmp);
}

static
int uniqv(int c, unsigned *v)
{
    int i, j;
    for (i = 0, j = 0; i < c; i++) {
       while (i + 1 < c && v[i] == v[i+1])
           i++;
       v[j++] = v[i];
    }
    assert(j <= c);
    return j;
}

static
int rand_range(int min, int max)
{
    assert(max >= min);
    return min + rand() % (max - min + 1);
}

static
int make_random_set(unsigned **pv, int *pbpp)
{
    int bpp = *pbpp = rand_range(10, 10);
    int c = rand_range(16, 16);
    fprintf(stderr, "bpp=%d, c=%d\n", bpp, c);
    unsigned *v = *pv = malloc(c * sizeof(unsigned));
    int i;
    unsigned mask = ~0u;
    if (bpp < 32)
	mask = (1 << bpp) - 1;
    for (i = 0; i < c; i++)
	v[i] = rand() & mask;
    sortv(c, v);
    c = uniqv(c, v);
    return c;
}

static
void test_random_set(void)
{
    unsigned *v;
    int bpp;
    int c = make_random_set(&v, &bpp);
    test_set(c, v, bpp);
}

int main()
{
    unsigned v[] = {
	0x020a, 0x07e5, 0x3305, 0x35f5,
	0x4980, 0x4c4f, 0x74ef, 0x7739,
	0x82ae, 0x8415, 0xa3e7, 0xb07e,
	0xb584, 0xb89f, 0xbb40, 0xf39e,
    };
    int c = sizeof(v) / sizeof(unsigned);
    int bpp = 16;
    if (0)
    test_set(c, v, bpp);
    int i;
    for (i = 0; i < 1000; i++)
	test_random_set();
    return 0;
}

// ex: set ts=8 sts=4 sw=4 noet:
