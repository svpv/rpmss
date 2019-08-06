#include <string.h>
#include <stdint.h>
#include <stdbool.h>
/*
 * To compare two decoded sets, we basically need to compare two arrays
 * of sorted numbers, v1[] and v2[].  This can be done with a merge-like
 * routine, which advances either v1 or v2 at each step (or both, when
 * two elements match).  We do something like this, but with a few twists.
 *
 * Note that, when comparing Requires against Provides, the Requires set
 * is usually sparse:
 *
 *	Provides (v1): a b c d e f g h i j k l ...
 *	Requires (v2): a   c         h   j     ...
 *
 * A specialized loop can skip Provides towards the next Requires element.
 * The loop might look like this:
 *
 *	while (v1 < v1end && *v1 < *v2)
 *	    v1++;
 *
 * The first condition, a boundary check, can be eliminated if we install
 * V_MAX sentinel at v1end.
 *
 * Moreover, when the Requires set is very sparse, it makes sense to step
 * a few elements at a time, and then somehow to step a little bit back.
 * This will require more than one sentinel.
 */
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
    /* The comparison loop template. */
#define CMPLOOP(N, ADV)		\
    do {			\
	while (1) {		\
	    IFLT ## N(ADV);	\
	    IFGE;		\
	}			\
    } while (0)
    /* All the boundary checking is done inside the loop (this is because
     * e.g. in the IFLT part, only the v1 boundary needs to be checked).
     * To facilitate the checking even further, we preload the boundaries
     * into registers. */
    const unsigned *v1end = v1 + n1;
    const unsigned *v2end = v2 + n2;
    /* Since v2 gets advanced far less often than v1, its value is also
     * preloaded into a register.  Preloading v1 also helps a little bit. */
    unsigned v1val = *v1;
    unsigned v2val = *v2;
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
    if (v1val < v2val) {	\
	le = 0;			\
	v1++;			\
	ADVANCE_V1_ ## ADV(1);	\
	if (v1 == v1end)	\
	    return ge ? 1 : -2; \
	v1val = *v1;		\
    }
#define IFGE			\
    if (v1val == v2val) {	\
	v1++, v2++;		\
	/* We don't use "do { STMT } while (0)" hygienic macros,
	 * because we need to break out of the enclosing loop. */ \
	if (v1 == v1end)	\
	    break;		\
	if (v2 == v2end)	\
	    break;		\
	v1val = *v1;		\
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
    if (v1val < v2val) {	\
	le = 0;			\
	v1 += 2;		\
	ADVANCE_V1_ ## ADV(2);	\
	if (v1[-1] < v2val)	\
	    (void) 0;		\
	else			\
	    v1--;		\
	if (v1 == v1end)	\
	    return ge ? 1 : -2; \
	v1val = *v1;		\
    }
#define IFLT4(ADV)		\
    if (v1val < v2val) {	\
	le = 0;			\
	v1 += 4;		\
	ADVANCE_V1_ ## ADV(4);	\
	if (v1[-2] < v2val)	\
	    v1 -= 1;		\
	else			\
	    v1 -= 3;		\
	if (*v1 < v2val)	\
	    v1++;		\
	if (v1 == v1end)	\
	    return ge ? 1 : -2; \
	v1val = *v1;		\
    }
#define IFLT8(ADV)		\
    if (v1val < v2val) {	\
	le = 0;			\
	v1 += 8;		\
	ADVANCE_V1_ ## ADV(8);	\
	if (v1[-4] < v2val)	\
	    v1 -= 2;		\
	else			\
	    v1 -= 6;		\
	if (*v1 < v2val)	\
	    v1++;		\
	else			\
	    v1--;		\
	if (*v1 < v2val)	\
	    v1++;		\
	if (v1 == v1end)	\
	    return ge ? 1 : -2; \
	v1val = *v1;		\
    }
    /* Choose the right loop:
     * if n1/n2 < CROSSOVER, use a less speculative one. */
#define CROSSOVER 44
    /* Here n2 can be safely multiplied by up to 32, see a comment
     * on the maximum set-string size in rpmss.c:encodeInit(). */
    bool smallstep = CROSSOVER > 32 && sizeof n2 < 5 ?
	    n1 / 2 < CROSSOVER / 2 * n2 :
	    n1 / 1 < CROSSOVER / 1 * n2 ;
    /* At least on Ivy Bridge and Haswell, the best code is obtained with
     * just a single crossover between the two loops.  Should you profile
     * the code again, be sure to check the real counter of CPU cycles,
     * as opposed to valgrind instruction reads; i.e. it makes sense
     * to execute more instructions with fewer mispredicted branches. */
    if (smallstep)
	CMPLOOP(2, UNROLLED);
    else
	CMPLOOP(4, UNROLLED);
    /* If CMPLOOP(8) gets used, SENTINELS should be set to 8. */
#define SENTINELS 4
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
    memset(v + n, 0xff, SENTINELS * sizeof(*v));
}

/*
 * Recall that the elements of a set are not necessarily full 32-bit
 * integers; sets explicitly express their bpp parameter, bits per value.
 * Two sets with different bpp can still be meaningfully compared,
 * provided that lower bits of full 32-bit hash were used as bpp hash.
 * In this case, the set with bigger bpp can be "downsampled" to match
 * the smaller bpp set: higher bits stripped, and elements sorted again.
 *
 * Note, however, that most of the time, downsampling will only be needed
 * for Provides versions (due to new exported symbols), and hash values
 * will be reduced only by 1 bit.  Reduction by 1 bit can be implemented
 * without sorting the values again.  Indeed, only a merge is required.
 * The array v[] can be split into two parts: the first part v1[] and
 * the second part v2[], the latter having values with high bit set.
 * After the high bit is stripped, v2[] values are still sorted.
 * It suffices to merge v1[] and v2[].
 */
/* Reduce a set of (bpp + 1) values to a set of bpp values. */
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

/* Cache entry holds the decoded set v[n] for the given set-string str.
 * Each entry is allocated in a single malloc chunk. */
struct cache_ent {
    int len;
    int n;
    char str[];
    /* After null-terminated str[], there goes v[n], properly aligned.
     * Provide some macros to deal with str[] and access v[]. */
#define ENT_STRSIZE(len) ((len + sizeof(unsigned)) & ~(sizeof(unsigned)-1))
#define ENT_V(ent, len) ((unsigned *)(ent->str + ENT_STRSIZE(len)))
};

/* The cache of this size (about 256 entries) can provide
 * 75% hit ratio while using less than 2MB of malloc chunks. */
#define CACHE_SIZE (256 - 1) /* Need sentinel */

/* We use LRU cache with a special first-time insertion policy.
 * When adding an element to the cache for the first time,
 * pushing it to the front tends to assign "extra importance"
 * to that new element, at the expense of other elements that are
 * already in the cache.  The idea is then to try first-time
 * insertion somewhere in the middle.  Further attempts suggest
 * that the midpoint should actually be closer to the end. */
#define MIDPOINT (CACHE_SIZE * 7 / 8)

/* On a hit, move to front that many steps. */
#define MOVSTEP 32

struct cache {
    /* We use a separate array of hash(ent->str) values.
     * The search is first done on this array, without touching
     * the entries.  Note that hv[] goes first and gets the best
     * alignment, which might facilitate the search. */
    uint16_t hv[CACHE_SIZE + 1];
    /* Total count, initially less than CACHE_SIZE. */
    int hc;
    /* Cache entries. */
    struct cache_ent *ev[CACHE_SIZE];
};

/* need malloc */
#include <stdlib.h>
#define xmalloc malloc

/* need rpmssDecode */
#include "rpmss.h"

static struct stats {
    int hit;
    int miss;
} stats;

static inline unsigned hash16(const char *str, unsigned len)
{
    uint32_t h;
    memcpy(&h, str + 4, 4);
    h *= 2654435761U;
    h += len << 16;
    return h >> 16;
}

#ifdef __SSE2__
#include <emmintrin.h>
#endif

static int cache_decode(struct cache *c,
			const char *str, int len,
			int n /* expected v[] size */,
			const unsigned **pv)
{
    int i;
    struct cache_ent *ent;
    uint16_t *hv = c->hv;
    struct cache_ent **ev = c->ev;
    unsigned hash = hash16(str, len);
#ifdef __SSE2__
    __m128i xmm0 = _mm_set1_epi16(hash);
#endif
    // Install sentinel
    hv[c->hc] = hash;
    uint16_t *hp = hv;
    while (1) {
	// Find hash
#ifdef __SSE2__
	unsigned mask;
	do {
	    __m128i xmm1 = _mm_loadu_si128((void *)(hp + 0));
	    __m128i xmm2 = _mm_loadu_si128((void *)(hp + 8));
	    hp += 16;
	    xmm1 = _mm_cmpeq_epi16(xmm1, xmm0);
	    xmm2 = _mm_cmpeq_epi16(xmm2, xmm0);
	    xmm1 = _mm_packs_epi16(xmm1, xmm2);
	    mask = _mm_movemask_epi8(xmm1);
	} while (mask == 0);
	hp -= 16;
	hp += __builtin_ctz(mask);
#else
	while (1) {
	    // Cf. Quicker sequential search in [Knuth, Vol.3, p.398]
	    if (hp[0] == hash) break;
	    if (hp[1] == hash) { hp += 1; break; }
	    if (hp[2] == hash) { hp += 2; break; }
	    if (hp[3] == hash) { hp += 3; break; }
	    hp += 4;
	}
#endif
	i = hp - hv;
	// Found sentinel?
	if (i == c->hc)
	    break;
	// Found an entry
	ent = ev[i];
	// Recheck the entry
	if (len != ent->len || memcmp(str, ent->str, len)) {
	    hp++;
	    continue;
	}
	// Hit, move to front
	if (i > MOVSTEP) {
	    hv += (unsigned) i - MOVSTEP;
	    ev += (unsigned) i - MOVSTEP;
	    memmove(hv + 1, hv, MOVSTEP * sizeof hv[0]);
	    memmove(ev + 1, ev, MOVSTEP * sizeof ev[0]);
	    hv[0] = hash;
	    ev[0] = ent;
	}
	stats.hit++;
	*pv = ENT_V(ent, len);
	return ent->n;
    }
    stats.miss++;
    // decode
    ent = xmalloc(sizeof(*ent) + ENT_STRSIZE(len) + (n + SENTINELS) * sizeof(unsigned));
    unsigned *v = ENT_V(ent, len);
    n = rpmssDecode(str, v);
    if (n <= 0) {
	free(ent);
	return n;
    }
    install_sentinels(v, n);
    ent->len = len;
    ent->n = n;
    memcpy(ent->str, str, len + 1);
    // insert
    if (c->hc <= MIDPOINT)
	i = c->hc++;
    else {
	// free last entry
	if (c->hc < CACHE_SIZE)
	    c->hc++;
	else
	    free(ev[CACHE_SIZE - 1]);
	// position at the midpoint
	i = MIDPOINT;
	memmove(hv + i + 1, hv + i, (CACHE_SIZE - i - 1) * sizeof hv[0]);
	memmove(ev + i + 1, ev + i, (CACHE_SIZE - i - 1) * sizeof ev[0]);
    }
    hv[i] = hash;
    ev[i] = ent;
    *pv = v;
    return n;
}

#include <stdio.h>
static __attribute__((destructor)) void print_stats(void)
{
    fprintf(stderr, "rpmsetcmp cache %.1f%% hit rate\n",
	    100.0 * stats.hit / (stats.hit + stats.miss));
}

/* The real cache.  You can make it __thread. */
static struct cache C;

/* Decode small Provides version without caching.
 * Merely touching the cache is relatively expensive; also,
 * the existing cache entries should not be discarded too easily. */
#define DECODE_CACHE_SIZE 256

/* Limit stack memory usage */
#define DECODE_STACK_SIZE 1024

/* API */
#include "rpmsetcmp.h"

int rpmsetcmp(const char *s1, const char *s2)
{
    // initialize decoding
    int bpp1;
    int len1 = strlen(s1);
    int n1 = rpmssDecodeInit(s1, len1, &bpp1);
    if (n1 < 0)
	return -11;
    int bpp2;
    int len2 = strlen(s2);
    int n2 = rpmssDecodeInit(s2, len2, &bpp2);
    if (n2 < 0)
	return -12;

    /* Unknown error, cannot happen. */
    int cmp = -13;

    /* This is the final continuation; v1[] and v2[] names
     * are not known yet, but their sizes are n1 and n2. */
#define SETCMP(v1, v2)					\
    do {						\
	cmp = setcmp(v1, n1, v2, n2);			\
    } while (0)

    /* Decoding Provides has some asymmetries: cache_decode
     * returns read-only buffer (which cannot be recycled)
     * with the sentinels already allocated and installed. */
#define DECODE_PROVIDES2(SENTINELS, NEXTC, NEXT)	\
    do {						\
        if (n1 >= DECODE_CACHE_SIZE) {			\
	    const unsigned *v1;				\
	    n1 = cache_decode(&C, s1, len1, n1, &v1);	\
	    if (n1 <= 0) {				\
		cmp = -11;				\
		break;					\
	    }						\
	    NEXTC;					\
        } else {					\
	    unsigned v1[n1 + SENTINELS];		\
	    n1 = rpmssDecode(s1, v1);			\
	    if (n1 <= 0) {				\
		cmp = -11;				\
		break;					\
	    }						\
	    NEXT;					\
	}						\
    } while (0)

    /* Pass SENTINELS or NO_SENTINELS to be used in NEXT. */
#define NO_SENTINELS 0

    /* Sometimes Provides are handled symmetrically. */
#define DECODE_PROVIDES(SENTINELS, NEXT)		\
	DECODE_PROVIDES2(SENTINELS, NEXT, NEXT)

    /* Simplify v[] array allocation. */
#define vmalloc(n) xmalloc((n) * sizeof(unsigned))

    /* Decoding Requires is always symmetrical. */
#define DECODE_REQUIRES(NEXT)				\
    do {						\
        if (n2 > DECODE_STACK_SIZE) {			\
	    unsigned *v2 = vmalloc(n2);			\
	    n2 = rpmssDecode(s2, v2);			\
	    if (n2 <= 0) {				\
		free(v2);				\
		cmp = -12;				\
		break;					\
	    }						\
	    NEXT;					\
	    free(v2);					\
        } else {					\
	    unsigned v2[n2];				\
	    n2 = rpmssDecode(s2, v2);			\
	    if (n2 <= 0) {				\
		cmp = -12;				\
		break;					\
	    }						\
	    NEXT;					\
	}						\
    } while (0)

    /* Sentinels are only needed for Provides, which might
     * change its name from v1 to w, but still uses n1. */
#define INSTALL_SENTINELS(v, NEXT)			\
    do {						\
	install_sentinels(v, n1);			\
	NEXT;						\
    } while (0)

    /* Now we're ready to handle the simple case
     * in which downsampling is not needed. */
    if (bpp1 == bpp2) {
	DECODE_PROVIDES2(SENTINELS,
	    /* cache has sentinels */
		DECODE_REQUIRES(SETCMP(v1, v2)),
	    INSTALL_SENTINELS(v1,
		DECODE_REQUIRES(SETCMP(v1, v2))));
	return cmp;
    }

    /* Simplify malloc/stack processing which does not require
     * error handling and breaking out of the NEXT. */
#define ALLOC(w, n, NEXT)				\
    do {						\
	if (n > DECODE_STACK_SIZE) {			\
	    unsigned *w = vmalloc(n);			\
	    NEXT;					\
	    free(w);					\
        } else {					\
	    unsigned w[n];				\
	    NEXT;					\
	}						\
    } while (0)

    /* Downsample either Provides or Requires. */
#define DOWNSAMPLE1(v, n, w, bpp, NEXT)			\
    do {						\
	n = downsample1(v, n, w, bpp);			\
	NEXT;						\
    } while (0)

    /* Now can handle two more cases. */
    if (bpp1 == bpp2 + 1) {
	DECODE_PROVIDES(NO_SENTINELS,
	    ALLOC(w, n1 + SENTINELS,
		DOWNSAMPLE1(v1, n1, w, bpp2,
		    INSTALL_SENTINELS(w,
			DECODE_REQUIRES(SETCMP(w, v2))))));
	return cmp;
    }
    if (bpp2 == bpp1 + 1) {
	DECODE_PROVIDES2(SENTINELS,
	    /* cache has sentinels */
DECODE_REQUIRES(ALLOC(w, n2, DOWNSAMPLE1(v2, n2, w, bpp1, SETCMP(v1, w)))),
	    INSTALL_SENTINELS(v1,
DECODE_REQUIRES(ALLOC(w, n2, DOWNSAMPLE1(v2, n2, w, bpp1, SETCMP(v1, w))))));
	return cmp;
    }

    /* Simplify double buffer allocation. */
#define ALLOC2(w1, w2, n, SENTINELS, NEXT)		\
	ALLOC(w0, n * 2 + SENTINELS,			\
	ALLOC2_NEXT(w0, w1, w2, n, NEXT))
#define ALLOC2_NEXT(w0, w1, w2, n, NEXT)		\
    do {						\
	unsigned *w1 = w0;				\
	unsigned *w2 = w0 + n;				\
	NEXT;						\
    } while (0)

    /* Simplify conversion from buffer to pointer. */
#define RENAME(v, w, NEXT)				\
    do {						\
	unsigned *w = v;				\
	NEXT;						\
    } while (0)

    /* Downsample either Requires or Provides using w1 and w2
     * alternate buffers; w1 and w2 must be pointers, they will be
     * freely exchanged; w2 may point to v; the output is in w1. */
#define DOWNSAMPLE(v, n, w1, w2, bppG, bppL, NEXT)	\
    do {						\
	bppG--;						\
	n = downsample1(v, n, w1, bppG);		\
	do {						\
	    bppG--;					\
	    n = downsample1(w1, n, w2, bppG);		\
	    unsigned *wx = w1;				\
	    w1 = w2;					\
	    w2 = wx;					\
	} while (bppG > bppL);				\
	NEXT;						\
    } while (0)

    /* Handle the most difficult cases. */
    if (bpp1 > bpp2) {
	DECODE_PROVIDES2(SENTINELS,
	    ALLOC2(w, w2, n1, SENTINELS,
DOWNSAMPLE(v1, n1, w, w2, bpp1, bpp2, INSTALL_SENTINELS(w, DECODE_REQUIRES(SETCMP(w, v2))))),
	    ALLOC(w0, n1 + SENTINELS,
		RENAME(w0, w,
		    RENAME(v1, w2,
DOWNSAMPLE(v1, n1, w, w2, bpp1, bpp2, INSTALL_SENTINELS(w, DECODE_REQUIRES(SETCMP(w, v2))))))));
	return cmp;
    }

    /* bpp2 > bpp1 */
    {
	DECODE_PROVIDES2(SENTINELS,
	    /* cache has sentinels */
DECODE_REQUIRES(ALLOC(w0, n2, RENAME(w0, w, RENAME(v2, w2, DOWNSAMPLE(v2, n2, w, w2, bpp2, bpp1, SETCMP(v1, w)))))),
	    INSTALL_SENTINELS(v1,
DECODE_REQUIRES(ALLOC(w0, n2, RENAME(w0, w, RENAME(v2, w2, DOWNSAMPLE(v2, n2, w, w2, bpp2, bpp1, SETCMP(v1, w))))))));
	return cmp;
    }
}

// ex: set ts=8 sts=4 sw=4 noet:
