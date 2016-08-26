#include <assert.h>
#include "rpmss.h"

/*
 * Encoding is performed in three logical steps.
 *
 * 1) Delta encoding: a sorted sequence of integer values
 * is replaced by the sequence of their differences.
 *
 * Initial dv is taken to be v[0].  Two consecutive numbers
 * are represented with dv=0 (e.g. v=[1,2,4] yields dv=[1,0,1]);
 * therefore, the values in v[] must be unique.
 *
 * 2) Golomb-Rice coding: integers are compressed into bits.
 *
 * The idea is as follows.  Input values are assumed to be small
 * integers.  Each value is split into two parts: an integer resulting
 * from its higher bits and an integer resulting from its lower bits
 * (with the number of lower bits specified by the special m parameter).
 * The first integer, called q, is then stored in unary coding (which is
 * a variable-length sequence of 0 bits followed by a terminating 1);
 * the second part, called r, the remainder, is stored in normal binary
 * coding (using m bits).
 *
 * The method is justified by the fact that, since most of the values
 * are small, their first parts will be short (typically 1..3 bits).
 * In particular, the method is known to be optimal for uniformly
 * distributed hash values, after the values are sorted and
 * delta-encoded; see [1] and also [2] for more general treatment.
 *
 * 3) Base62 armor: bits are serialized with alphanumeric characters.
 *
 * We implement a base64-based base62 encoding which is similar, but not
 * identical to, the one described in [3].  To encode 6 bits, we need 64
 * characters, but we have only 62.  Missing characters are 62 = 111110
 * and 63 = 111111.  Therefore, if the lower 5 bits are 11110 (which is
 * 30 or 'U') or 11111 (which is 31 or 'V' - in terms of [0-9A-Za-z]),
 * we encode only five bits (using 'U' or 'V'); the sixth high bit is
 * left for the next character.  When the last few bits are issued,
 * missing high bits are assumed to be 0; no further special handling
 * is required in this case.
 *
 * Overall, a set-string (also called a set-version in rpm) looks like
 * this: "set:bMxyz...". The "set:" prefix marks set-versions in rpm
 * (to distinguish them between regular rpm versions).  It is assumed
 * to be stripped here.  The next two characters (denoted 'b' and 'M')
 * encode two parameters: bpp using [a-z] and m using [A-Z]. Their valid
 * ranges are 7..32 and 5..30, respectively.  Also, valid m must be less
 * than bpp.  The rest ("xyz...") is a variable-length encoded sequence.
 *
 * References
 * [1] Felix Putze, Peter Sanders, Johannes Singler (2007)
 *     Cache-, Hash- and Space-Efficient Bloom Filters
 * [2] Alistair Moffat, Andrew Turpin (2002)
 *     Compression and Coding Algorithms
 * [3] Kejing He, Xiancheng Xu, Qiang Yue (2008)
 *     A Secure, Lossless, and Compressed Base62 Encoding
 * [4] A. Kiely (2004)
 *     Selecting the Golomb Parameter in Rice Coding
 */

static int encodeInit(const unsigned *v, int n, int bpp)
{
    /* No empty sets */
    if (n < 1)
	return -1;

    /* Validate bpp */
    if (bpp < 7 || bpp > 32)
	return -2;

    /* Last value must fit within bpp range */
    if (bpp < 32 && v[n - 1] >> bpp)
	return -3;

    /* Last value must be consistent with the sequence */
    if (v[n - 1] < (unsigned) n - 1)
	return -4;

    /* Average delta */
    unsigned dv = (v[n - 1] - n + 1) / n;

    /* Select m */
    int m = 5;
    if (dv < 32) {
	/*
	 * It is possible that they try to encode too many values using
	 * too small bpp range, which will not only result in suboptimal
	 * encoding, but also can break estmation of n based on bpp and m.
	 */
	if (n >= (1 << (bpp - m)))
	    return -5;
    }
    else {
	/*
	 * When dv > 66 > 2^6, switch to use m = 6, and so on.
	 * Generally dv > 2^m.
	 */
	unsigned range = 66;
	while (dv > range) {
	    m++;
	    if (m == 30)
		break;
	    range = range * 2 + 1;
	}
    }

    /*
     * By construction, 2^m < dv < 2^{bpp}/n, which implies n < 2^{bpp-m}.
     * When bpp and m are known, we can use this to estimate maximum n.
     * Also, note that the sum of n deltas cannot overflow bpp range.
     */
    assert(n < (1 << (bpp - m)));

    /* This also implies that m < bpp */
    assert(m < bpp);
    return m;
}

int rpmssEncodeInit(const unsigned *v, int n, int bpp)
{
    int m = encodeInit(v, n, bpp);
    if (m < 0)
	return m;

    /* Need at least (m + 1) bits per value */
    int bits1 = n * (m + 1);

    /*
     * The second term is much tricker: assuming that remainders are small,
     * q deltas must have enough room to cover the whole range.
     */
    int bits2 = (v[n - 1] - n + 1) >> m;

    /*
     * Five bits can make a character, as well as the remaining bits; also
     * need two leading characters, and the string must be null-terminated.
     */
    return (bits1 + bits2) / 5 + 4;
}

static const char bits2char[] = "0123456789"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz";

int rpmssEncode(const unsigned *v, int n, int bpp, char *s)
{
    int m = encodeInit(v, n, bpp);
    if (m < 0)
	return m;

    /* Put bpp and m */
    const char *s_start = s;
    *s++ = bpp - 7 + 'a';
    *s++ = m - 5 + 'A';

    /* Delta */
    unsigned v0, v1, dv;
    unsigned vmax = v[n - 1];
    const unsigned *v_end = v + n;

    /* Golomb */
    int q;
    unsigned r;
    unsigned rmask = (1u << m) - 1;

    /* Pending bits */
    unsigned b = 0;
    /* Reuse n for pending bit count */
    n = 0;

    /* Make initial delta */
    v0 = *v++;
    if (v0 > vmax)
	return -10;
    dv = v0;

    /*
     * Loop invariant: either (n < 5), or (n == 5) but the 5 pending bits
     * do not form irregular case - i.e. nothing to flush.
     */
    while (1) {
	/* Put q */
	q = dv >> m;
	/* Add zero bits */
	n += q;
	if (n >= 6) {
	    /*
	     * By the loop invariant, only regular cases are possible.
	     * (Note that irregular cases need the 5th bit set, but
	     * we have only added zeros.)
	     */
	    *s++ = bits2char[b];
	    n -= 6;
	    b = 0;
	    /* Only zeroes left */
	    while (n >= 6) {
		*s++ = '0';
		n -= 6;
	    }
	}
	/*
	 * Add stop bit.  We then have at least 1 bit and at most 6 bits.
	 * If we do have 6 bits, it is not possible that the lower 5 bits
	 * form an irregular case.  Therefore, with the next character
	 * flushed, no q bits, including the stop bit, will be left.
	 */
	b |= (1u << n);
	n++;

	/* Put r */
	r = dv & rmask;
	b |= (r << n);
	n += m;

	/* Got at least 6 bits (due to m >= 5), ready to flush */
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
	    /* First run consumes non-r part completely, see above */
	    b = r >> (m - n);
	} while (n >= 6);

	/* Flush pending irregular case */
	if (n == 5) {
	    switch (b) {
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

	/* Loop control */
	if (v == v_end)
	    break;

	/* Make next delta */
	v1 = *v++;
	if (v1 <= v0)
	    return -11;
	if (v1 > vmax)
	    return -12;
	dv = v1 - v0 - 1;
	v0 = v1;
    }

    /*
     * Last character with high bits defaulting to zero.
     * Only regular cases are possible.
     */
    if (n)
	*s++ = bits2char[b];
    *s = '\0';
    return s - s_start;
}

static int decodeInit(const char *s, int *pbpp)
{
    int bpp = *s++ - 'a' + 7;
    if (bpp < 7 || bpp > 32)
	return -1;
    int m = *s++ - 'A' + 5;
    if (m < 5 || m > 30)
	return -2;
    if (m >= bpp)
	return -3;
    if (*s == '\0')
	return -4;
    *pbpp = bpp;
    return m;
}

/* This version tries to estimate output size by only looking
 * at parameters, without actually knowing string length.
 * Such estimate would require special handling of malformed
 * set-strings in rpmssDecode. */
#if 0
int rpmssDecodeInit1(const char *s, int *pbpp)
{
    int m = decodeInit(s, pbpp);
    if (m < 0)
	return m;
    return (1 << (*pbpp - m)) - 1;
}
#endif

int rpmssDecodeInit(const char *s, int len, int *pbpp)
{
    int m = decodeInit(s, pbpp);
    if (m < 0)
	return m;
    /* XXX validate len */
    int n1 = (1 << (*pbpp - m)) - 1;
    /* Each character will fill at most 6 bits */
    int bits = (len - 2) * 6;
    /* Each (m + 1) bits can make a value */
    int n2 = bits / (m + 1);
    /* Whichever smaller */
    if (n2 < n1)
	return n2;
    return n1;
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

int rpmssDecode(const char *s, unsigned *v)
{
    int bpp;
    int m = decodeInit(s, &bpp);
    if (m < 0)
	return m;
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
	    goto putQ;
	case W_05:
	    b &= 0x0fff;
	    n = 5;
	    goto putQ;
	default:
	    // bad input
	    return -20;
	}
    }

    /* Template for getQ and getR coroutines */
#define Get(X) \
    { \
	long w = *(unsigned short *) s; \
	b = word2bits[w]; \
	/* the most common case: 12 bits */ \
	if (b < 0x1000) { \
	    /* further try to combine 12+12 or 12+11 bits */ \
	    w = *(unsigned short *) (s + 2); \
	    unsigned bx = word2bits[w]; \
	    if (bx < 0x1000) { \
		s += 4; \
		b |= (bx << 12); \
		n = 24; \
		goto put ## X; \
	    } \
	    if (bx < 0x2000) { \
		s += 4; \
		bx &= 0x0fff; \
		b |= (bx << 12); \
		n = 23; \
		goto put ## X; \
	    } \
	    s += 2; \
	    n = 12; \
	    goto put ## X; \
	} \
	/* the second most common case: 11 bits */ \
	if (b < 0x2000) { \
	    b &= 0x0fff; \
	    /* further try to combine 11+12 bits */ \
	    w = *(unsigned short *) (s + 2); \
	    unsigned bx = word2bits[w]; \
	    if (bx < 0x1000) { \
		s += 4; \
		b |= (bx << 11); \
		n = 23; \
		goto put ## X; \
	    } \
	    s += 2; \
	    n = 11; \
	    goto put ## X; \
	} \
	/* less common cases: fewer bits and/or eol */ \
	s += 2; \
	switch (b & 0xf000) { \
	case W_10: \
	    b &= 0x0fff; \
	    n = 10; \
	    goto put ## X; \
	case W_06: \
	    b &= 0x0fff; \
	    n = 6; \
	    goto putlast ## X; \
	case W_05: \
	    b &= 0x0fff; \
	    n = 5; \
	    goto putlast ## X; \
	case W_00: \
	    goto puteol ## X; \
	default: \
	    /* bad input */ \
	    return -21; \
	} \
    }

    /* Actually define getQ and getR coroutines */
getQ:
    Get(Q);
getR:
    Get(R);

    /* golomb pieces */
#define InitR \
    r |= (b << rfill); \
    rfill += n
#define MakeR(getR) \
    { \
	int left = rfill - m; \
	if (left < 0) \
	    goto getR; \
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
	n = left; \
    }
#define MakeQ(getQ) \
    { \
	if (b == 0) { \
	    q += n; \
	    goto getQ; \
	} \
	int vbits = __builtin_ffs(b); \
	n -= vbits; \
	b >>= vbits; \
	q += vbits - 1; \
	qmax -= q; \
	if (qmax < 0) \
	    return -13; \
	r = b; \
	rfill = n; \
    }

putQ:
    MakeQ(getQ); MakeR(getR);
    // at most 18 left
    MakeQ(getQ); MakeR(getR);
    // at most 12 left
    MakeQ(getQ); MakeR(getR);
    // at most 6 left
    MakeQ(getQ); MakeR(getR);
    goto getQ;
putR:
    InitR;
    MakeR(getR);
    // at most 23 left
    MakeQ(getQ); MakeR(getR);
    // at most 17 left
    MakeQ(getQ); MakeR(getR);
    // at most 11 left
    MakeQ(getQ); MakeR(getR);
    // at most 5 left
    MakeQ(getQ); goto getR;

    /* Handle end of input */
putlastQ:
    MakeQ(nomoreQ);
    MakeR(nomoreR);
    goto check;
puteolQ:
    /* up to 5 trailing zero bits */
    if (q > 5)
	return -20;
    goto check;
putlastR:
    InitR;
    MakeR(getR);
    /* only zero bits left */
    if (b != 0)
	return -21;
    goto check;
puteolR:
    /* cannot complete the value */
    return -22;
nomoreQ:
    return -23;
nomoreR:
    return -24;

    /* check before successful return */
check:
    return v - v_start;

}

// ex: set ts=8 sts=4 sw=4 noet:
