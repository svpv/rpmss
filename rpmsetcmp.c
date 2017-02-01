/*
 * To compare two decoded sets, we basically need to compare two arrays
 * of sorted numbers, v1[] and v2[].  This can be done with a merge-like
 * algorithm, which advances either v1 or v2 at each step (or both, when
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
 * a few elements at a time, and then step back a little bit using bisecting
 * (cf. Binary merging in [Knuth, Vol.3, p.203]).  This requires more than
 * one sentinel.
 */
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
#define CACHE_SIZE (256 - 2) /* Need sentinel and count */

/* We use LRU cache with a special first-time insertion policy.
 * When adding an element to the cache for the first time,
 * pushing it to the front tends to assign "extra importance"
 * to that new element, at the expense of other elements that are
 * already in the cache.  The idea is then to try first-time
 * insertion somewhere in the middle.  Further attempts suggest
 * that the midpoint should actually be closer to the end. */
#define MIDPOINT (CACHE_SIZE * 7 / 8)

struct cache {
    /* We use a separate array of hash(ent->str) values.
     * The search is first done on this array, without touching
     * the entries.  Note that hv[] goes first and gets the best
     * alignment, which might facilitate the search. */
    unsigned hv[CACHE_SIZE + 1];
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

static int cache_decode(struct cache *c,
			const char *str, int len,
			int n /* expected v[] size */,
			const unsigned **pv)
{
    int i;
    struct cache_ent *ent;
    unsigned *hv = c->hv;
    struct cache_ent **ev = c->ev;
    unsigned hash;
    memcpy(&hash, str, sizeof hash);
    hash ^= len;
    // Install sentinel
    hv[c->hc] = hash;
    unsigned *hp = hv;
    while (1) {
	// Find hash
	while (1) {
	    // Cf. Quicker sequential search in [Knuth, Vol.3, p.398]
	    if (hp[0] == hash) break;
	    if (hp[1] == hash) { hp += 1; break; }
	    if (hp[2] == hash) { hp += 2; break; }
	    if (hp[3] == hash) { hp += 3; break; }
	    hp += 4;
	}
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
	if (i) {
	    memmove(hv + 1, hv, i * sizeof hv[0]);
	    memmove(ev + 1, ev, i * sizeof ev[0]);
	    hv[0] = hash;
	    ev[0] = ent;
	}
	*pv = ENT_V(ent, len);
	return ent->n;
    }
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
