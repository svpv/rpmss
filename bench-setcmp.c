#include "rpmsetcmp.h"
#include "rpmsetcmp.c"

struct two {
    int n1, n2;
    unsigned *v1, *v2;
};

#define MAXTWOS (1<<20)
static struct two twos[MAXTWOS];
static int ntwos;

#include <stdlib.h>
#include <assert.h>
#include "rpmss.h"

static int decode(const char *s, size_t len, unsigned **pv, int *pbpp)
{
    if (strncmp(s, "set:", 4) == 0)
	s += 4;
    int n = rpmssDecodeInit(s, len, pbpp);
    assert(n > 0);
    unsigned *v = malloc((n + SENTINELS) * sizeof(unsigned));
    assert(v);
    n = rpmssDecode(s, v);
    assert(n > 0);
    install_sentinels(v, n);
    *pv = v;
    return n;
}

static void dotwo(const char *s1, size_t len1,
		  const char *s2, size_t len2)
{
    unsigned *v1, *v2;
    int bpp1, bpp2;
    int n1 = decode(s1, len1, &v1, &bpp1);
    int n2 = decode(s2, len2, &v2, &bpp2);
    while (bpp1 > bpp2) {
	unsigned *w = malloc((n1 + SENTINELS) * sizeof(unsigned));
	n1 = downsample1(v1, n1, w, --bpp1);
	install_sentinels(w, n1);
	free(v1);
	v1 = w;
    }
    while (bpp2 > bpp1) {
	unsigned *w = malloc((n2 + SENTINELS) * sizeof(unsigned));
	n2 = downsample1(v2, n2, w, --bpp2);
	install_sentinels(w, n2);
	free(v2);
	v2 = w;
    }
    assert(ntwos < MAXTWOS);
    twos[ntwos++] = (struct two) { n1, n2, v1, v2 };
}

#include <stdio.h>

static void readlines(void)
{
    char *line = NULL;
    size_t alloc_size = 0;
    ssize_t len;
    while ((len = getline(&line, &alloc_size, stdin)) >= 0) {
	if (len > 0 && line[len-1] == '\n')
	    line[--len] = '\0';
	char *s1 = line;
	char *s2 = strchr(line, ' ');
	if (s2 == NULL)
	    s2 = strchr(line, '\t');
	assert(s2);
	*s2++ = '\0';
	dotwo(s1, s2 - s1, s2, s1 + len - s2);
	line = NULL;
	alloc_size = 0;
    }
}

#include "rpmsetcmp.h"

static void setcmpall(void)
{
    for (int i = 0; i < ntwos; i++) {
	struct two *two = twos + i;
	int ret = setcmp(two->v1, two->n1, two->v2, two->n2);
	assert(ret != 42);
    }
}

#include "bench.h"

int main()
{
    readlines();
    BENCH(setcmpall);
    return 0;
}
