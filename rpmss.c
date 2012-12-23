#include <stdio.h>
#include <assert.h>
#include "rpmss.h"

/*
 * References
 * 1. Felix Putze, Peter Sanders, Johannes Singler (2007)
 *    Cache-, Hash- and Space-Efficient Bloom Filters
 * 2. A. Kiely (2004)
 *    Selecting the Golomb Parameter in Rice Coding
 * 3. Alistair Moffat, Andrew Turpin (2002)
 *    Compression and Coding Algorithms
 * 4. Kejing He, Xiancheng Xu, Qiang Yue (2008)
 *    A Secure, Lossless, and Compressed Base62 Encoding
 */

static
int encodeInit(const unsigned *v, int n, int bpp)
{
    // no empty sets
    if (n < 1)
	return -1;
    // validate bpp
    if (bpp < 8 || bpp > 32)
	return -2;
    // last value must fit within bpp range
    if (bpp < 32 && v[n - 1] >> bpp)
	return -3;
    // last value must be consistent with the sequence
    if (v[n - 1] < (unsigned) n - 1)
	return -4;
    // average dv
    unsigned dv = (v[n - 1] - n + 1) / n;
    // select m
    int m = 6;
    unsigned range = 133;
    while (dv > range) {
	m++;
	if (m == 31)
	    break;
	range = range * 2 + 1;
    }
    assert(m < bpp);
    return m;
}

int rpmssEncodeSize(const unsigned *v, int n, int bpp)
{
    int m = encodeInit(v, n, bpp);
    if (m < 0)
	return m;
    // need at least (m + 1) bits per value
    int bits1 = n * (m + 1);
    // the second term is much tricker: assuming that remainders are small,
    // q deltas must have enough room to cover the whole range
    int bits2 = (v[n - 1] - n + 1) >> m;
    // five bits can make a character, as well as the remaining bits; also
    // need two leading characters, and the string must be null-terminated
    return (bits1 + bits2) / 5 + 4;
}

static
const char bits2char[] = "0123456789"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz";

int rpmssEncode(const unsigned *v, int n, int bpp, char *s)
{
    int m = encodeInit(v, n, bpp);
    if (m < 0)
	return m;
    // put parameters
    const char *s_start = s;
    *s++ = bpp - 8 + 'a';
    *s++ = m - 6 + 'a';
    // delta
    unsigned v0 = (unsigned) -1;
    unsigned v1, dv;
    unsigned vmax = v[n - 1];
    const unsigned *v_start = v;
    const unsigned *v_end = v + n;
    // golomb
    int q;
    unsigned r;
    unsigned rmask = (1u << m) - 1;
    // pending bits
    unsigned b = 0;
    // reuse n for pending bit count
    n = 0;
    // at the start of each iteration, either (n < 5), or (n == 5) but
    // the 5 pending bits do not form irregular case; this allows some
    // shortcuts when putting q: for (n >= 6), only regular cases are
    // possible - either by the starting condition, or by the virtue
    // of preceding zero bits (since irregular cases need 5th bit set)
    do {
	// make dv
	v0++;
	if (v0 == 0 && v != v_start)
	    return -10;
	v1 = *v++;
	if (v1 < v0)
	    return -11;
	if (v1 > vmax)
	    return -12;
	dv = v1 - v0;
	v0 = v1;
	// put q
	q = dv >> m;
	n += q;
	if (n >= 6) {
	    // only regular case is possible
	    *s++ = bits2char[b];
	    n -= 6;
	    b = 0;
	    // only zeroes left
	    while (n >= 6) {
		*s++ = '0';
		n -= 6;
	    }
	}
	// impossible to make irregular 6 bits
	b |= (1 << n);
	n++;
	// put r
	r = dv & rmask;
	b |= (r << n);
	n += m;
	do {
	    switch (b & 31) {
	    case 30:
		*s++ = 'U';
		n -= 5;
		break;
	    case 31:
		*s++ = 'V';
		n -= 5;
		break;
	    default:
		*s++ = bits2char[b & 63];
		n -= 6;
		break;
	    }
	    // first run consumes non-r part completely
	    b = r >> (m - n);
	}
	while (n >= 6);
	// flush pending irregular case
	if (n == 5) {
	    switch (b & 31) {
	    case 30:
		*s++ = 'U';
		n = 0;
		b = 0;
		break;
	    case 31:
		*s++ = 'V';
		n = 0;
		b = 0;
		break;
	    }
	}
    }
    while (v < v_end);
    // only regular case is possible
    if (n)
	*s++ = bits2char[b];
    *s = '\0';
    return s - s_start;
}

static
int decodeInit(const char *s, int len, int *pbpp)
{
    int bpp = *s++ - 'a' + 8;
    if (bpp < 8 || bpp > 32)
	return -1;
    int m = *s++ - 'a' + 6;
    if (m < 6 || m > 31)
	return -2;
    if (m >= bpp)
	return -3;
    if (*s == '\0')
	return -4;
    if (len < 4)
	return -5;
    //if (s[len] != '\0')
	//return -6;
    *pbpp = bpp;
    return m;
}

int rpmssDecodeSize(const char *s, int len)
{
    int bpp;
    int m = decodeInit(s, len, &bpp);
    if (m < 0)
	return m;
    // each character will fill at most 6 bits
    int bits = (len - 2) * 6;
    // each (m + 1) bits can make a value
    return bits / (m + 1);
}

// Word types (when two bytes from base62 string cast to unsigned short).
enum {
    W_12 = 0x0000,
    W_11 = 0x1000,
    W_10 = 0x2000,
    W_06 = 0x3000,  /* last 6 bits */
    W_05 = 0x4000,  /* last 5 bits */
    W_00 = 0x5000,  /* end of line */
    W_EE = 0xeeee,
};

// for BYTE_ORDER
#include <sys/types.h>

// Combine two characters into array index (with respect to endianness).
#if BYTE_ORDER && BYTE_ORDER == LITTLE_ENDIAN
#define CCI(c1, c2) ((c1) | ((c2) << 8))
#elif BYTE_ORDER && BYTE_ORDER == BIG_ENDIAN
#define CCI(c1, c2) ((c2) | ((c1) << 8))
#else
#error "unknown byte order"
#endif

// Maps base62 word into numeric value (decoded bits) ORed with word type.
static
const unsigned short word2bits[65536] = {
    // default to error
    [0 ... 65535] = W_EE,
    // macros to initialize regions
#define R1(w, s, c1, c2, b1, b2) [CCI(c1, c2)] = w | (c1 - b1) | ((c2 - b2) << s)
#define R1x2(w, s, c1, c2, b1, b2) R1(w, s, c1, c2, b1, b2), R1(w, s, c1, c2 + 1, b1, b2)
#define R1x3(w, s, c1, c2, b1, b2) R1(w, s, c1, c2, b1, b2), R1x2(w, s, c1, c2 + 1, b1, b2)
#define R1x5(w, s, c1, c2, b1, b2) R1x2(w, s, c1, c2, b1, b2), R1x3(w, s, c1, c2 + 2, b1, b2)
#define R1x6(w, s, c1, c2, b1, b2) R1(w, s, c1, c2, b1, b2), R1x5(w, s, c1, c2 + 1, b1, b2)
#define R1x10(w, s, c1, c2, b1, b2) R1x5(w, s, c1, c2, b1, b2), R1x5(w, s, c1, c2 + 5, b1, b2)
#define R1x20(w, s, c1, c2, b1, b2) R1x10(w, s, c1, c2, b1, b2), R1x10(w, s, c1, c2 + 10, b1, b2)
#define R1x26(w, s, c1, c2, b1, b2) R1x20(w, s, c1, c2, b1, b2), R1x6(w, s, c1, c2 + 20, b1, b2)
#define R2x26(w, s, c1, c2, b1, b2) R1x26(w, s, c1, c2, b1, b2), R1x26(w, s, c1 + 1, c2, b1, b2)
#define R3x26(w, s, c1, c2, b1, b2) R1x26(w, s, c1, c2, b1, b2), R2x26(w, s, c1 + 1, c2, b1, b2)
#define R5x26(w, s, c1, c2, b1, b2) R2x26(w, s, c1, c2, b1, b2), R3x26(w, s, c1 + 2, c2, b1, b2)
#define R6x26(w, s, c1, c2, b1, b2) R1x26(w, s, c1, c2, b1, b2), R5x26(w, s, c1 + 1, c2, b1, b2)
#define R10x26(w, s, c1, c2, b1, b2) R5x26(w, s, c1, c2, b1, b2), R5x26(w, s, c1 + 5, c2, b1, b2)
#define R20x26(w, s, c1, c2, b1, b2) R10x26(w, s, c1, c2, b1, b2), R10x26(w, s, c1 + 10, c2, b1, b2)
#define R26x26(w, s, c1, c2, b1, b2) R20x26(w, s, c1, c2, b1, b2), R6x26(w, s, c1 + 20, c2, b1, b2)
#define R2x10(w, s, c1, c2, b1, b2) R1x10(w, s, c1, c2, b1, b2), R1x10(w, s, c1 + 1, c2, b1, b2)
#define R3x10(w, s, c1, c2, b1, b2) R1x10(w, s, c1, c2, b1, b2), R2x10(w, s, c1 + 1, c2, b1, b2)
#define R5x10(w, s, c1, c2, b1, b2) R2x10(w, s, c1, c2, b1, b2), R3x10(w, s, c1 + 2, c2, b1, b2)
#define R6x10(w, s, c1, c2, b1, b2) R1x10(w, s, c1, c2, b1, b2), R5x10(w, s, c1 + 1, c2, b1, b2)
#define R10x10(w, s, c1, c2, b1, b2) R5x10(w, s, c1, c2, b1, b2), R5x10(w, s, c1 + 5, c2, b1, b2)
#define R20x10(w, s, c1, c2, b1, b2) R10x10(w, s, c1, c2, b1, b2), R10x10(w, s, c1 + 10, c2, b1, b2)
#define R26x10(w, s, c1, c2, b1, b2) R20x10(w, s, c1, c2, b1, b2), R6x10(w, s, c1 + 20, c2, b1, b2)
#define R2x1(w, s, c1, c2, b1, b2) R1(w, s, c1, c2, b1, b2), R1(w, s, c1 + 1, c2, b1, b2)
#define R3x1(w, s, c1, c2, b1, b2) R1(w, s, c1, c2, b1, b2), R2x1(w, s, c1 + 1, c2, b1, b2)
#define R5x1(w, s, c1, c2, b1, b2) R2x1(w, s, c1, c2, b1, b2), R3x1(w, s, c1 + 2, c2, b1, b2)
#define R6x1(w, s, c1, c2, b1, b2) R1(w, s, c1, c2, b1, b2), R5x1(w, s, c1 + 1, c2, b1, b2)
#define R10x1(w, s, c1, c2, b1, b2) R5x1(w, s, c1, c2, b1, b2), R5x1(w, s, c1 + 5, c2, b1, b2)
#define R20x1(w, s, c1, c2, b1, b2) R10x1(w, s, c1, c2, b1, b2), R10x1(w, s, c1 + 10, c2, b1, b2)
#define R26x1(w, s, c1, c2, b1, b2) R20x1(w, s, c1, c2, b1, b2), R6x1(w, s, c1 + 20, c2, b1, b2)
#define R10x2(w, s, c1, c2, b1, b2) R10x1(w, s, c1, c2, b1, b2), R10x1(w, s, c1, c2 + 1, b1, b2)
#define R26x2(w, s, c1, c2, b1, b2) R26x1(w, s, c1, c2, b1, b2), R26x1(w, s, c1, c2 + 1, b1, b2)
#define R2x2(w, s, c1, c2, b1, b2) R2x1(w, s, c1, c2, b1, b2), R2x1(w, s, c1, c2 + 1, b1, b2)
#define R1x40(w, s, c1, c2, b1, b2) R1x20(w, s, c1, c2, b1, b2), R1x20(w, s, c1, c2 + 20, b1, b2)
#define R1x80(w, s, c1, c2, b1, b2) R1x40(w, s, c1, c2, b1, b2), R1x40(w, s, c1, c2 + 40, b1, b2)
#define R1x160(w, s, c1, c2, b1, b2) R1x80(w, s, c1, c2, b1, b2), R1x80(w, s, c1, c2 + 80, b1, b2)
#define R1x200(w, s, c1, c2, b1, b2) R1x40(w, s, c1, c2, b1, b2), R1x160(w, s, c1, c2 + 40, b1, b2)
#define R1x220(w, s, c1, c2, b1, b2) R1x20(w, s, c1, c2, b1, b2), R1x200(w, s, c1, c2 + 20, b1, b2)
#define R1x230(w, s, c1, c2, b1, b2) R1x10(w, s, c1, c2, b1, b2), R1x220(w, s, c1, c2 + 10, b1, b2)
#define R1x256(w, s, c1, c2, b1, b2) R1x26(w, s, c1, c2, b1, b2), R1x230(w, s, c1, c2 + 26, b1, b2)
    // both characters are regular
    R10x10(W_12, 6, '0', '0', '0', '0'),
    R10x26(W_12, 6, '0', 'A', '0', 'A' + 10),
    R10x26(W_12, 6, '0', 'a', '0', 'a' + 36),
    R26x10(W_12, 6, 'A', '0', 'A' + 10, '0'),
    R26x10(W_12, 6, 'a', '0', 'a' + 36, '0'),
    R26x26(W_12, 6, 'A', 'A', 'A' + 10, 'A' + 10),
    R26x26(W_12, 6, 'A', 'a', 'A' + 10, 'a' + 36),
    R26x26(W_12, 6, 'a', 'A', 'a' + 36, 'A' + 10),
    R26x26(W_12, 6, 'a', 'a', 'a' + 36, 'a' + 36),
    // first character irregular
    R2x10(W_11, 5, 'U', '0', 'A' + 10, '0'),
    R2x26(W_11, 5, 'U', 'A', 'A' + 10, 'A' + 10),
    R2x26(W_11, 5, 'U', 'a', 'A' + 10, 'a' + 36),
    // second character irregular
    R10x2(W_11, 6, '0', 'U', '0', 'A' + 10),
    R26x2(W_11, 6, 'A', 'U', 'A' + 10, 'A' + 10),
    R26x2(W_11, 6, 'a', 'U', 'a' + 36, 'A' + 10),
    // both characters irregular
    R2x2(W_10, 5, 'U', 'U', 'A' + 10, 'A' + 10),
    // last regular character
    R10x1(W_06, 6, '0', '\0', '0', '\0'),
    R26x1(W_06, 6, 'A', '\0', 'A' + 10, '\0'),
    R26x1(W_06, 6, 'a', '\0', 'a' + 36, '\0'),
    // last irregular character
    R2x1(W_05, 5, 'U', '\0', 'A' + 10, '\0'),
    // end of line
    R1x256(W_00, 0, '\0', '\0', '\0', '\0'),
};

int rpmssDecode(const char *s, int len, unsigned *v, int *pbpp)
{
    int bpp;
    int m = decodeInit(s, len, &bpp);
    if (m < 0)
	return m;
    *pbpp = bpp;
    int left, vbits;
    // delta
    unsigned v0 = (unsigned) -1;
    unsigned v1, dv;
    unsigned vmax = ~0u;
    if (bpp < 32)
	vmax = (1u << bpp) - 1;
    const unsigned *v_start = v;
    // golomb
    int q = 0;
    unsigned r = 0;
    int rfill = 0;
    unsigned rmask = (1u << m) - 1;
    int qmax = (1 << (bpp - m)) - 1;
    // pending bits
    int n = 0;
    unsigned b = 0;
    // skip over parameters
    s += 2;
    // align
    if (1 & (long) s) {
	char buf[3];
	char *p = buf;
	if (1 & (long) p)
	    p++;
	p[0] = *s++;
	p[1] = '\0';
	long w = *(unsigned short *) p;
	b = word2bits[w];
	switch (b & 0xf000) {
	case W_06:
	    b &= 0x0fff;
	    n = 6;
	    goto putq;
	case W_05:
	    b &= 0x0fff;
	    n = 5;
	    goto putq;
	default:
	    // bad input
	    return -20;
	}
    }

    /* Template for getq and getr coroutines */
#define Get(X) \
    { \
	long w = *(unsigned short *) s; \
	s += 2; \
	b = word2bits[w]; \
	/* the most common case: 12 bits */ \
	if (b < 0x1000) { \
	    /* further try to combine 12+12 or 12+11 bits */ \
	    w = *(unsigned short *) s; \
	    unsigned bx = word2bits[w]; \
	    if (bx < 0x1000) { \
		s += 2; \
		b |= (bx << 12); \
		n = 24; \
		goto put ## X; \
	    } \
	    if (bx < 0x2000) { \
		s += 2; \
		bx &= 0x0fff; \
		b |= (bx << 12); \
		n = 23; \
		goto put ## X; \
	    } \
	    n = 12; \
	    goto put ## X; \
	} \
	/* the second most common case: 11 bits */ \
	if (b < 0x2000) { \
	    b &= 0x0fff; \
	    /* further try to combine 11+12 bits */ \
	    w = *(unsigned short *) s; \
	    unsigned bx = word2bits[w]; \
	    if (bx < 0x1000) { \
		s += 2; \
		b |= (bx << 11); \
		n = 23; \
		goto put ## X; \
	    } \
	    n = 11; \
	    goto put ## X; \
	} \
	/* less common cases: fewer bits and/or eol */ \
	switch (b & 0xf000) { \
	case W_10: \
	    b &= 0x0fff; \
	    n = 10; \
	    goto put ## X; \
	case W_06: \
	    b &= 0x0fff; \
	    goto put06 ## X; \
	case W_05: \
	    b &= 0x0fff; \
	    goto put05 ## X; \
	case W_00: \
	    goto put00 ## X; \
	default: \
	    /* bad input */ \
	    return -21; \
	} \
    }

    /* Actually define getq and getr coroutines */
getq:
    Get(q);
getr:
    Get(r);

    /* golomb pieces */
#define RInit \
    r |= (b << rfill); \
    rfill += n
#define RMake \
    left = rfill - m; \
    if (left < 0) \
	goto getr; \
    r &= rmask; \
    dv = (q << m) | r; \
    v0++; \
    if (v == 0 && v != v_start) \
	return -10; \
    v1 = v0 + dv; \
    if (v1 < v0) \
	return -11; \
    if (v1 > vmax) \
	return -12; \
    *v++ = v1; \
    v0 = v1; \
    q = 0; \
    b >>= n - left; \
    n = left
#define QMake \
    if (b == 0) { \
	q += n; \
	goto getq; \
    } \
    vbits = __builtin_ffs(b); \
    n -= vbits; \
    b >>= vbits; \
    q += vbits - 1; \
    qmax -= q; \
    if (qmax < 0) \
	return -13; \
    r = b; \
    rfill = n
putq:
    QMake; RMake;
    // at most 17 left
    QMake; RMake;
    // at most 10 left
    QMake; RMake;
    // at most 3 left
    QMake; goto getr;
putr:
    RInit;
    RMake;
    // at most 23 left
    QMake; RMake;
    // at most 16 left
    QMake; RMake;
    // at most 9 left
    QMake; RMake;
    // at most 2 left
    QMake; goto getr;

    /* Handle end of input */
put06q:
put05q:
    /* cannot complete the value */
    return -27;
put00q:
    /* up to 5 trailing zero bits */
    if (q > 5)
	return -29;
    /* successful return */
    return v - v_start;
put06r:
    n = 6;
    goto put01r;
put05r:
    n = 5;
put01r:
    RInit;
    RMake;
    /* only zero bits left */
    if (b != 0)
	return -21;
    /* successful return */
    return v - v_start;
put00r:
    /* cannot complete the value */
    return -23;
}

// ex: set ts=8 sts=4 sw=4 noet:
