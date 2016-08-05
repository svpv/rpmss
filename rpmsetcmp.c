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
    /* choose the right stepper */
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

struct cache_ent {
    char *str;
    int len;
    int n;
    unsigned v[];
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
 * that the "midpoint" or "pivot" should be closer to the end. */
#define PIVOT_SIZE (CACHE_SIZE * 7 / 8)

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

static int cache_decode(struct cache *c, const char *str, const unsigned **pv)
{
    int i;
    struct cache_ent *ent;
    unsigned *hv = c->hv;
    struct cache_ent **ev = c->ev;
    unsigned hash;
    memcpy(&hash, str, 4);
    // Install sentinel
    hv[c->hc] = hash;
    while (1) {
	// Find hash
	unsigned *hp = hv;
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
	// Found entry
	ent = ev[i];
	// Recheck entry
	if (memcmp(str, ent->str, ent->len + 1)) {
	    hp++;
	    continue;
	}
	// Hit, move to front
	if (i) {
	    memmove(hv + 1, hv, i * sizeof(hv[0]));
	    memmove(ev + 1, ev, i * sizeof(ev[0]));
	    hv[0] = hash;
	    ev[0] = ent;
	}
	*pv = ent->v;
	return ent->n;
    }
    // decode
    int len = strlen(str);
    int bpp;
    int n = rpmssDecodeInit2(str, len, &bpp);
    ent = xmalloc(sizeof(*ent) + len + 1 + (n + SENTINELS) * sizeof(unsigned));
    n = ent->n = rpmssDecode(str, ent->v);
    if (n <= 0) {
	free(ent);
	return n;
    }
    install_sentinels(ent->v, n);
    ent->str = (char *)(ent->v + n + SENTINELS);
    memcpy(ent->str, str, len + 1);
    ent->len = len;
    // insert
    if (c->hc < CACHE_SIZE)
	i = c->hc++;
    else {
	// free last entry
	free(ev[CACHE_SIZE - 1]);
	// position at midpoint
	i = PIVOT_SIZE;
	memmove(hv + i + 1, hv + i, (CACHE_SIZE - i - 1) * sizeof(hv[0]));
	memmove(ev + i + 1, ev + i, (CACHE_SIZE - i - 1) * sizeof(ev[0]));
    }
    hv[i] = hash;
    ev[i] = ent;
    *pv = ent->v;
    return n;
}

/* The real cache.  You can make it __thread. */
static struct cache cache;

/* Decode small Provides version without caching */
#define PROV_STACK_SIZE 256

/* Stage 2a: downsample set1 */
static int setcmp2a(const unsigned *v1o, unsigned *v1, unsigned *vx,
	int n1, int bpp1,
	const unsigned *v2, int n2, int bpp2)
{
    bpp1--;
    n1 = downsample1(v1o, n1, v1, bpp1);
    while (bpp1 > bpp2) {
	bpp1--;
	n1 = downsample1(v1, n1, vx, bpp1);
	unsigned *tmp = v1;
	v1 = vx;
	vx = tmp;
    }
    install_sentinels(v1, n1);
    return setcmp(v1, n1, v2, n2);
}

/* Stage 2: decode set1 (Provides) */
static int setcmp2(const char *s1, int n1, int bpp1,
	const unsigned *v2, int n2, int bpp2)
{
    /* need to downsample */
    if (bpp1 > bpp2) {
	/* decode using cache */
	if (n1 > PROV_STACK_SIZE) {
	    const unsigned *v1o;
	    n1 = cache_decode(&cache, s1, &v1o);
	    if (n1 <= 0) {
		return -11;
	    }
	    unsigned *v1 = xmalloc(n1 * 2 + SENTINELS);
	    int cmp = setcmp2a(v1o, v1, v1 + n1, n1, bpp1, v2, n2, bpp2);
	    free(v1);
	    return cmp;
	}
	/* decode on the stack */
	unsigned v1[n1 * 2 + SENTINELS];
	n1 = rpmssDecode(s1, v1);
	if (n1 <= 0)
	    return -11;
	return setcmp2a(v1, v1 + n1, v1, n1, bpp1, v2, n2, bpp2);
    }
    /* will not downsample */
    if (n1 > PROV_STACK_SIZE) {
	const unsigned *v1;
	n1 = cache_decode(&cache, s1, &v1);
	if (n1 <= 0) {
	    return -11;
	}
	int cmp = setcmp(v1, n1, v2, n2);
	return cmp;
    }
    else {
	unsigned v1[n1 + SENTINELS];
	n1 = rpmssDecode(s1, v1);
	if (n1 <= 0)
	    return -11;
	install_sentinels(v1, n1);
	return setcmp(v1, n1, v2, n2);
    }
}

/* Stage 1a: decode set2 with downsampling */
static int setcmp1a(const char *s1, int n1, int bpp1,
	const char *s2, int n2, int bpp2, unsigned *v2)
{
    n2 = rpmssDecode(s2, v2);
    if (n2 <= 0)
	return -12;
    unsigned *vx = v2 + n2;
    do {
	bpp2--;
	n2 = downsample1(v2, n2, vx, bpp2);
	unsigned *tmp = v2;
	v2 = vx;
	vx = tmp;
    } while (bpp2 > bpp1);
    return setcmp2(s1, n1, bpp1, v2, n2, bpp2);
}

/* Limit stack memory usage */
#define REQ_STACK_SIZE 1024

/* Stage 1: decode set2 (Requires) */
static int setcmp1(const char *s1, int n1, int bpp1,
	const char *s2, int n2, int bpp2)
{
    /* need to downsample */
    if (bpp2 > bpp1) {
	/* decode using malloc */
	if (n2 > REQ_STACK_SIZE / 2) {
	    unsigned *v2 = xmalloc(n2 * 2);
	    int cmp = setcmp1a(s1, n1, bpp1, s2, n2, bpp2, v2);
	    free(v2);
	    return cmp;
	}
	/* decode on the stack */
	unsigned v2[n2 * 2];
	return setcmp1a(s1, n1, bpp1, s2, n2, bpp2, v2);
    }
    /* will not downsample */
    if (n2 > REQ_STACK_SIZE) {
	unsigned *v2 = xmalloc(n2);
	n2 = rpmssDecode(s2, v2);
	if (n2 <= 0) {
	    free(v2);
	    return -12;
	}
	int cmp = setcmp2(s1, n1, bpp1, v2, n2, bpp2);
	free(v2);
	return cmp;
    }
    unsigned v2[n2];
    n2 = rpmssDecode(s2, v2);
    if (n2 <= 0)
	return -12;
    return setcmp2(s1, n1, bpp1, v2, n2, bpp2);
}

int rpmsetcmp(const char *s1, const char *s2)
{
    // initialize decoding
    int bpp1;
    int n1 = rpmssDecodeInit1(s1, &bpp1);
    if (n1 < 0)
	return -11;
    int bpp2;
    int n2 = rpmssDecodeInit1(s2, &bpp2);
    if (n2 < 0)
	return -12;
    // run comparison stages
    return setcmp1(s1, n1, bpp1, s2, bpp2, n2);
}

// ex: set ts=8 sts=4 sw=4 noet:
