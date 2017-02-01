static int downsample1(const unsigned *v, int n, unsigned *w, int bpp)
{
    unsigned mask = (1U << bpp) - 1;
    /* Find the first element with high bit set. */
    int l = 0;
    int u = n;
    while (l < u) {
	int i = (l + u) / 2;
	if (v[i] <= mask)
	    l = i + 1;
	else
	    u = i;
    }
    /* Initialize parts. */
    const unsigned *w_start = w;
    const unsigned *v1 = v + 0, *v1end = v + u;
    const unsigned *v2 = v + u, *v2end = v + n;
    /* Merge v1 and v2 into w. */
    if (v1 < v1end && v2 < v2end) {
	unsigned v1val = *v1;
	unsigned v2val = *v2 & mask;
	while (1) {
	    if (v1val < v2val) {
		*w++ = v1val;
		v1++;
		if (v1 == v1end)
		    break;
		v1val = *v1;
	    }
	    else if (v2val < v1val) {
		*w++ = v2val;
		v2++;
		if (v2 == v2end)
		    break;
		v2val = *v2 & mask;
	    }
	    else {
		*w++ = v1val;
		v1++;
		v2++;
		if (v1 == v1end)
		    break;
		if (v2 == v2end)
		    break;
		v1val = *v1;
		v2val = *v2 & mask;
	    }
	}
    }
    /* Append what's left. */
    while (v1 < v1end)
	*w++ = *v1++;
    while (v2 < v2end)
	*w++ = *v2++ & mask;
    /* The number of values may decrease. */
    return w - w_start;
}

struct decoded {
    int n;
    int bpp;
    unsigned v[];
};

#define MAXDD (1<<20)
static struct decoded *dd[MAXDD];
static int ndd;

#include <stdbool.h>
#include <string.h>
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
	ret += downsample1(d->v, d->n, w, d->bpp);
    }
}

#include "bench.h"

int main()
{
    readlines();
    BENCH(downsample);
    return 0;
}
