#include "rpmsetcmp.h"
#include "rpmsetcmp.c"

struct decoded {
    int n;
    int bpp;
    unsigned v[];
};

#define MAXDD (1<<20)
static struct decoded *dd[MAXDD];
static int ndd;

#include <stdbool.h>
#include <assert.h>
#include "rpmss.h"

static bool doline(const char *line, size_t len)
{
    if (strncmp(line, "set:", 4) == 0)
	line += 4;
    int bpp;
    int n = rpmssDecodeInit(line, len, &bpp);
    assert(n > 0);
    struct decoded *d = dd[ndd++] =
	    malloc(sizeof(struct decoded) + n * sizeof(unsigned));
    assert(d);
    d->n = rpmssDecode(line, d->v);
    assert(d->n > 0);
    assert(d->n <= n);
    d->bpp = bpp;
    return ndd == MAXDD;
}

#include <stdio.h>
#include <stdlib.h>

static void readlines(void)
{
    char *line = NULL;
    size_t alloc_size = 0;
    ssize_t len;
    while ((len = getline(&line, &alloc_size, stdin)) >= 0) {
	if (len > 0 && line[len-1] == '\n')
	    line[--len] = '\0';
	if (len == 0)
	    continue;
	if (doline(line, len))
	    break;
    }
    free(line);
}

#define MAXW (1<<20)
unsigned w[MAXW];
static volatile unsigned ret;

static void downsample(void)
{
    for (int i = 0; i < ndd; i++) {
	struct decoded *d = dd[i];
	assert(d->n <= MAXW);
	ret += downsample1(d->v, d->n, w, d->bpp - 1);
    }
}

#include "bench.h"

int main()
{
    readlines();
    BENCH(downsample);
    return 0;
}
