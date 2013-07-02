#include <assert.h>

#define SENTINELS 8
#define STACK_PROV_SIZE 256
#define REQ_STACK_SIZE 1024

static void cache_lock(void)
{
}

static void cache_unlock(void)
{
}

// Reduce a set of (bpp + 1) values to a set of bpp values.
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

/* Stage 2: decode set1 (Provides) */
static int setcmp2(const char *s1, int n1, int bpp1, int m1,
	const unsigned *v2, int n2, int bpp2)
{
    if (n1 > STACK_PROV_SIZE) {
	// decode s1 using cache
	const unsigned *v1;
	cache_lock();
	n1 = cache_decode(&v1);
	if (n1 < 0) {
	    cache_unlock();
	    return -11;
	}
	int cmp = setcmp3(v1, n1, v2, n2);
	cache_unlock();
	free(v2);
	return cmp;
    }
    else {
	// decode s1 on the stack
	unsigned v1[n1 + SENTINELS];
	n1 = decode(v1);
	add_sentinels(v1, n1);
	int cmp = setcmp(v1, n1, v2, n2);
	free(v2);
	return cmp;
    }
}

/* Stage 1a: decode set2 with downsampling */
static int setcmp1a(const char *s1, int n1, int bpp1, int m1,
	const char *s2, int n2, int bpp2, int m2, unsigned *v2)
{
    n2 = rpmssDecode(s2, bpp2, m2, v2);
    if (n2 <= 0)
	return -12;
    unsigned *vx = v2 + n2;
    do {
	n2 = downsample(v2, n2, vx, bpp2);
	bpp2--;
	unsigned *vtmp = v2;
	v2 = vx;
	vx = vtmp;
    } while (bpp2 > bpp1);
    return setcmp2(s1, n1, bpp1, m1, v2, n2, bpp2);
}

/* Limit stack memory usage */
#define REQ_STACK_SIZE 1024

/* Stage 1: decode set2 (Requires) */
static int setcmp1(const char *s1, int n1, int bpp1, int m1,
	const char *s2, int n2, int bpp2, int m2)
{
    /* need to downsample */
    if (bpp2 > bpp1) {
	/* decode using malloc */
	if (n2 > REQ_STACK_SIZE / 2) {
	    unsigned *v2 = xmalloc(n2 * 2);
	    int cmp = setcmp1a(s1, n1, bpp1, m1, s2, n2, bpp2, m2, v2);
	    free(v2);
	    return cmp;
	}
	/* decode on the stack */
	unsigned v2[n2 * 2];
	return setcmp1a(s1, n1, bpp1, m1, s2, n2, bpp2, m2, v2);
    }
    /* will not downsample */
    if (n2 > REQ_STACK_SIZE) {
	unsigned *v2 = xmalloc(n2);
	n2 = rpmssDecode(s2, bpp2, m2, v2);
	if (n2 <= 0) {
	    free(v2);
	    return -12;
	}
	int cmp = setcmp2(s1, n1, bpp1, v2, n2, bpp2);
	free(v2);
	return cmp;
    }
    unsigned v2[n2];
    n2 = rpmssDecode(s2, bpp2, m2, v2);
    if (n2 <= 0)
	return -12;
    return setcmp2(s1, n1, bpp1, m1, v2, n2, bpp2);
}

int rpmsetcmp(const char *s1, const char *s2)
{
    // initialize decoding
    int bpp1, bpp2, m1, m2;
    int n1 = rpmssDecodeInit(s1, &bpp1, &m1);
    if (n1 < 0)
	return -11;
    int n2 = rpmssDecodeInit(s2, &bpp2, &m2);
    if (n2 < 0)
	return -12;
    // run comparison stages
    return setcmp1(s1, n1, bpp1, m1, s2, bpp2, n2, m2);
}

// ex: set ts=8 sts=4 sw=4 noet:
