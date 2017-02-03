#include <string.h>
#include <stdbool.h>

static int setcmp(const unsigned *v1, size_t n1,
		  const unsigned *v2, size_t n2)
{
    /* Assume that the sets are equal.
     * These flags are cleared as the comparison progresses. */
    bool le = 1, ge = 1;
    /* The general structure of a set-comparison loop is as follows. */
#if defined(COMMENT) && !defined(COMMENT)
    while (1) {
	/* The "IFLT" part: */
	if (*v1 < *v2) {
	    /* There is an element in v1 which is not in v2.
	     * Therefore, v1 is not "less or equal" than v2. */
	    le = 0;
	    /* Advance v1 as much as possible, using a specialized loop.
	     * Sometimes, there will be a lot of elements to skip. */
	}
	/* The "IFGE" part: */
	if (*v1 == *v2) {
	    /* Advance v1 and v2, once; this is the second most probable
	     * outcome ("else", we probably have an unmet dependency). */
	}
	else {
	    /* There is an element in v2 which is not in v1.
	     * Therefore, v1 is not "greater or equal" than v2. */
	    ge = 0;
	    /* Advance v2, just once.  After this is done, the next
	     * comparison will probably yield (*v1 < *v2), which is
	     * indeed the very first branch in the loop. */
	}
    }
#endif
    /* All the boundary checking is done inside the loop (this is because
     * e.g. in the IFLT part, only the v1 boundary needs to be checked).
     * To facilitate the checking even further, we preload the boundaries
     * into registers. */
    const unsigned *v1end = v1 + n1;
    const unsigned *v2end = v2 + n2;
    /* Since v2 gets advanced far less often than v1, its value is also
     * preloaded into a register.  Whether v1 value should be preloaded
     * in the same manner, the results of profiling are inconclusive. */
    unsigned v2val = *v2;
    /* The comparison loop template. */
#define CMPLOOP(N, ADV)		\
    do {			\
	while (1) {		\
	    IFLT ## N(ADV);	\
	    IFGE;		\
	}			\
    } while (0)
    /* To advance v1, we provide two loops.  When the expected number
     * of iterations is very small, the "tight" loop should be used.
     * Otherwise, the "unrolled" loop can be beneficial.  Both loops
     * can advance v1 speculatively by more than 1 (which implies
     * that after the loop, v1 must somehow backtrack). */
#define ADVANCE_V1_TIGHT(N)	\
    while (*v1 < v2val)		\
	v1 += N;
#define ADVANCE_V1_UNROLLED(N)	\
    while (1) {			\
	/* Cf. Quicker sequential search in [Knuth, Vol.3, p.398] */ \
	/* Four iterations work best here. */		\
	if (v1[0*N] >= v2val) break;			\
	if (v1[1*N] >= v2val) { v1 += 1*N; break; }	\
	if (v1[2*N] >= v2val) { v1 += 2*N; break; }	\
	if (v1[3*N] >= v2val) { v1 += 3*N; break; }	\
	v1 += 4*N;		\
    }
    /* We're now able to provide a reference implementation for IFLT
     * and IFGE, thus completing the loop. */
#define IFLT1(ADV)		\
    if (*v1 < v2val) {		\
	le = 0;			\
	v1++;			\
	ADVANCE_V1_ ## ADV(1);	\
	/* We don't use "do { STMT } while (0)" hygienic macros,
	 * because we need to break out of the enclosing loop. */ \
	if (v1 == v1end)	\
	    break;		\
    }
#define IFGE			\
    if (*v1 == v2val) {		\
	v1++, v2++;		\
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
    /* Here come additional/specialized IFLT parts which advance v1
     * speculatively, after which they backtrack v1 using bisecting.
     * Cf. Binary merging in [Knuth, Vol.3, p.203] */
#define IFLT2(ADV)		\
    if (*v1 < v2val) {		\
	le = 0;			\
	v1 += 2;		\
	ADVANCE_V1_ ## ADV(2);	\
	v1--;			\
	if (*v1 < v2val)	\
	    v1++;		\
	if (v1 == v1end)	\
	    break;		\
    }
#define IFLT4(ADV)		\
    if (*v1 < v2val) {		\
	le = 0;			\
	v1 += 4;		\
	ADVANCE_V1_ ## ADV(4);	\
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
#define IFLT8(ADV)		\
    if (*v1 < v2val) {		\
	le = 0;			\
	v1 += 8;		\
	ADVANCE_V1_ ## ADV(8);	\
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
    /* Choose the right stepper.  We can safely multiply by 16 here, see
     * a comment on the maximum set-string size in rpmss.c:encodeInit().
     * The constant is derived empirically. */
    if (n1 >= 16 * n2)
	CMPLOOP(8, UNROLLED);
    else if (n1 >= 8 * n2)
	CMPLOOP(4, TIGHT);
    else
	CMPLOOP(1, TIGHT);
    /* If there are any elements left, this affects the result. */
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
