static int setcmp(const unsigned *v1, int n1, const unsigned *v2, int n2)
{
    /* Assume that the sets are equal.
     * These flags are cleared as the comparison progresses. */
    int ge = 1;
    int le = 1;
    const unsigned *v1end = v1 + n1;
    const unsigned *v2end = v2 + n2;
    unsigned v2val = *v2;
    /* loop pieces */
#define IFLT4			\
    if (*v1 < v2val) {		\
	le = 0;			\
	v1 += 4;		\
	while (*v1 < v2val)	\
	    v1 += 4;		\
	v1 -= 2;		\
	if (*v1 < v2val)	\
	    v1++;		\
	else			\
	    v1--;		\
	if (*v1 < v2val)	\
	    v1++;		\
	if (v1 == v1end)	\
	    break;		\
    }
#define IFLT8			\
    if (*v1 < v2val) {		\
	le = 0;			\
	v1 += 8;		\
	while (*v1 < v2val)	\
	    v1 += 8;		\
	v1 -= 4;		\
	if (*v1 < v2val)	\
	    v1 += 2;		\
	else			\
	    v1 -= 2;		\
	if (*v1 < v2val)	\
	    v1++;		\
	else			\
	    v1--;		\
	if (*v1 < v2val)	\
	    v1++;		\
	if (v1 == v1end)	\
	    break;		\
    }
#define IFGE			\
    if (*v1 == v2val) {		\
	v1++;			\
	v2++;			\
	if (v1 == v1end)	\
	    break;		\
	if (v2 == v2end)	\
	    break;		\
	v2val = *v2;		\
    }				\
    else {			\
	ge = 0;			\
	v2++;			\
	if (v2 == v2end)	\
	    break;		\
	v2val = *v2;		\
    }
    /* Choose the right stepper.  We can safely multiply by 16 here, see
     * a comment on the maximum set-string size in rpmss.c:encodeInit().
     * The constant is derived empirically. */
    if (n1 >= 16 * n2) {
	while (1) {
	    IFLT8;
	    IFGE;
	}
    }
    else {
	while (1) {
	    IFLT4;
	    IFGE;
	}
    }
    /* return */
    if (v1 < v1end)
	le = 0;
    if (v2 < v2end)
	ge = 0;
    if (le && ge)
	return 0;
    if (ge)
	return 1;
    if (le)
	return -1;
    return -2;
}

/* need memset */
#include <string.h>

/* The above technique requires sentinels properly installed
 * at the end of every Provides set. */
static inline void install_sentinels(unsigned *v, int n)
{
#define SENTINELS 8
    memset(v + n, 0xff, SENTINELS * sizeof(*v));
}

static int downsample1(const unsigned *v, size_t n, unsigned *w, int bpp)
{
    unsigned mask = (1U << bpp) - 1;
    /* Find the first element with high bit set. */
    size_t l = 0;
    size_t u = n;
    while (l < u) {
	size_t i = (l + u) / 2;
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
		    goto left2;
		v1val = *v1;
	    }
	    else if (v2val < v1val) {
		*w++ = v2val;
		v2++;
		if (v2 == v2end)
		    goto left1;
		v2val = *v2 & mask;
	    }
	    else {
		*w++ = v1val;
		v1++;
		v2++;
		if (v1 == v1end)
		    goto left2;
		if (v2 == v2end)
		    goto left1;
		v1val = *v1;
		v2val = *v2 & mask;
	    }
	}
    }
    /* In case no merge took place. */
    w = mempcpy(w, v1, (char *) v1end - (char *) v1);
left2:
    /* Append what's left. */
    while (v2 < v2end)
	*w++ = *v2++ & mask;
    /* The number of values may decrease. */
    return w - w_start;
left1:
    w = mempcpy(w, v1, (char *) v1end - (char *) v1);
    return w - w_start;
}

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
