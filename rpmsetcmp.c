#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "rpmss.h"

static void *xmalloc(size_t n) {
    return malloc(n);
}

/* Number of trailing sentinels in decoded Provides */
#define SENTINELS 8

static
int cache_decode(const char *str, const unsigned **pv)
{
    struct cache_ent {
	char *str;
	int len;
	int n;
	unsigned v[];
    };
#define CACHE_SIZE 256
#define PIVOT_SIZE 232
    static int hc;
    static unsigned hv[CACHE_SIZE + 1];
    static struct cache_ent *ev[CACHE_SIZE];
    // look up in the cache
    int i;
    struct cache_ent *ent;
    unsigned hash = str[0] | (str[2] << 8) | (str[3] << 16);
    unsigned *hp = hv;
    // Install sentinel
    hp[hc] = hash;
    while (1) {
	// Find hash
	while (*hp != hash)
	    hp++;
	i = hp - hv;
	// Found sentinel?
	if (i == hc)
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
#define SENTINELS 8
    ent = malloc(sizeof(*ent) + len + 1 + (n + SENTINELS) * sizeof(unsigned));
    assert(ent);
    n = ent->n = rpmssDecode(str, ent->v);
    if (n <= 0) {
	free(ent);
	return n;
    }
    for (i = 0; i < SENTINELS; i++)
	ent->v[n + i] = ~0u;
    ent->str = (char *)(ent->v + n + SENTINELS);
    memcpy(ent->str, str, len + 1);
    ent->len = len;
    // insert
    if (hc < CACHE_SIZE)
	i = hc++;
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

static void cache_lock(void)
{
}

static void cache_unlock(void)
{
}

/* Reduce a set of (bpp + 1) values to a set of bpp values. */
static int downsample(const unsigned *v, int n, unsigned *w, int bpp)
{
    unsigned mask = (1 << bpp) - 1;
    // find the first element with high bit set
    int l = 0;
    int u = n;
    while (l < u) {
	int i = (l + u) / 2;
	if (v[i] <= mask)
	    l = i + 1;
	else
	    u = i;
    }
    // initialize parts
    const unsigned *w_start = w;
    const unsigned *v1 = v + 0, *v1end = v + u;
    const unsigned *v2 = v + u, *v2end = v + n;
    // merge v1 and v2 into w
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
    // append what's left
    while (v1 < v1end)
	*w++ = *v1++;
    while (v2 < v2end)
	*w++ = *v2++ & mask;
    return w - w_start;
}

/* Compare decoded sets */
static int setcmp(const unsigned *v1, int n1, const unsigned *v2, int n2)
{
    int ge = 1;
    int le = 1;
    const unsigned *v1end = v1 + n1;
    const unsigned *v2end = v2 + n2;
    for (int i = 0; i < SENTINELS; i++)
	assert(v1end[i] == ~0u);
    unsigned v2val = *v2;
    // loop pieces
#define IFLT4 \
    if (*v1 < v2val) { \
	le = 0; \
	v1 += 4; \
	while (*v1 < v2val) \
	    v1 += 4; \
	v1 -= 2; \
	if (*v1 < v2val) \
	    v1++; \
	else \
	    v1--; \
	if (*v1 < v2val) \
	    v1++; \
	if (v1 == v1end) \
	    break; \
    }
#define IFLT8 \
    if (*v1 < v2val) { \
	le = 0; \
	v1 += 8; \
	while (*v1 < v2val) \
	    v1 += 8; \
	v1 -= 4; \
	if (*v1 < v2val) \
	    v1 += 2; \
	else \
	    v1 -= 2; \
	if (*v1 < v2val) \
	    v1++; \
	else \
	    v1--; \
	if (*v1 < v2val) \
	    v1++; \
	if (v1 == v1end) \
	    break; \
    }
#define IFGE \
    if (*v1 == v2val) { \
	v1++; \
	v2++; \
	if (v1 == v1end) \
	    break; \
	if (v2 == v2end) \
	    break; \
	v2val = *v2; \
    } \
    else { \
	ge = 0; \
	v2++; \
	if (v2 == v2end) \
	    break; \
	v2val = *v2; \
    }
    // choose the right stepper
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
    // return
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

/* Decode small Provides version without caching */
#define PROV_STACK_SIZE 256

/* Stage 2a: downsample set1 */
static int setcmp2a(const unsigned *v1o, unsigned *v1, unsigned *vx,
	int n1, int bpp1,
	const unsigned *v2, int n2, int bpp2)
{
    bpp1--;
    n1 = downsample(v1o, n1, v1, bpp1);
    while (bpp1 > bpp2) {
	bpp1--;
	n1 = downsample(v1, n1, vx, bpp1);
	unsigned *tmp = v1;
	v1 = vx;
	vx = tmp;
    }
    for (int i = 0; i < SENTINELS; i++)
	v1[n1 + i] = ~0u;
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
	    cache_lock();
	    const unsigned *v1o;
	    n1 = cache_decode(s1, &v1o);
	    if (n1 <= 0) {
		cache_unlock();
		return -11;
	    }
	    unsigned *v1 = xmalloc(n1 * 2 + SENTINELS);
	    int cmp = setcmp2a(v1o, v1, v1 + n1, n1, bpp1, v2, n2, bpp2);
	    cache_unlock();
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
	cache_lock();
	const unsigned *v1;
	n1 = cache_decode(s1, &v1);
	if (n1 <= 0) {
	    cache_unlock();
	    return -11;
	}
	int cmp = setcmp(v1, n1, v2, n2);
	cache_unlock();
	return cmp;
    }
    else {
	unsigned v1[n1 + SENTINELS];
	n1 = rpmssDecode(s1, v1);
	if (n1 <= 0)
	    return -11;
	for (int i = 0; i < SENTINELS; i++)
	    v1[n1 + i] = ~0u;
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
	n2 = downsample(v2, n2, vx, bpp2);
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
