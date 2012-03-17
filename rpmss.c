#include <stdio.h>
#include <assert.h>
#include "rpmss.h"

int rpmssEncodeSize(int c, int bpp)
{
    int bitc = c * 2 * bpp + 16;
    return bitc / 5 + 2;
}

static
int log2i(int n)
{
    int m = 0;
    while (n >>= 1)
	m++;
    return m;
}

static
const char bits2char[] = "0123456789"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz";

int rpmssEncode(int c, const unsigned *v, int bpp, char *s)
{
    // no empty sets
    if (c < 1)
	return -1;
    // validate bpp
    if (bpp < 10 || bpp > 32)
	return -2;
    // prepare golomb parameter
    int m = bpp - log2i(c) - 1;
    if (m < 7)
	m = 7;
    // put control chars
    const char *s_start = s;
    *s++ = bpp - 7 + 'a';
    *s++ = m - 7 + 'a';
    // delta encoding
    unsigned v0 = (unsigned) -1;
    unsigned v1, dv;
    unsigned vmax = ~0u;
    if (bpp < 32)
	vmax = (1 << bpp) - 1;
    // golomb encoding
    int q;
    unsigned r;
    const unsigned rmask = (1 << m) - 1;
    // pending bits
    int n = 0;
    unsigned b = 0;
    // handle values
    const unsigned *v_end = v + c;
    do {
	// make dv
	v0++;
	v1 = *v++;
	if (v1 > vmax)
	    return -4;
	if (v1 < v0)
	    return -5;
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

int rpmssDecodeSize(const char *s, int len)
{
    int bpp = *s++ + 7 - 'a';
    if (bpp < 10 || bpp > 32)
	return -1;
    int m = *s++ + 7 - 'a';
    if (m < 7 || m > 31)
	return -2;
    if (m >= bpp)
	return -3;
    if (*s == '\0')
	return -4;
    if (len < 4)
	return -1;
    int bitc = (len - 2) * 6;
    return bitc / (m + 1);
}

static
const unsigned char2bits[256] = {
    [0 ... 255] = 0xee,
    [0] = 0xff,
#define C1(c, b) [c] = c - b
#define C2(c, b) C1(c, b), C1(c + 1, b)
#define C5(c, b) C1(c, b), C2(c + 1, b), C2(c + 3, b)
#define C10(c, b) C5(c, b), C5(c + 5, b)
    C10('0', '0'),
#define C26(c, b) C1(c, b), C5(c + 1, b), C10(c + 6, b), C10(c + 16, b)
    C26('A', 'A' + 10),
    C26('a', 'a' + 36),
    ['U'] = 62,
    ['V'] = 63,
};

// Word types (when two bytes from base62 string cast to unsigned short).
enum {
    W_12 = 0x0000,
    W_11 = 0x1000,
    W_10 = 0x2000,
    W_06 = 0x3000,
    W_05 = 0x4000,
    W_00 = 0x5000,
    W_EE = 0xeeee,
};

// Combine two characters into array index (with respect to endianness).
#include <sys/types.h>
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
    // defaults to error
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
    R2x1(W_05, 5, 'U', '\0', 'A' + 36, '\0'),
    // end of line
    R1x256(W_00, 0, '\0', '\0', '\0', '\0'),
};

int rpmssDecode(const char *s, unsigned *v, int *pbpp)
{
    int bpp = *s++ + 7 - 'a';
    if (bpp < 10 || bpp > 32)
	return -1;
    int m = *s++ + 7 - 'a';
    if (m < 7 || m > 31)
	return -2;
    if (m >= bpp)
	return -3;
    if (*s == '\0')
	return -4;
    *pbpp = bpp;
    const unsigned *v_start = v;
    long w;
    int left, vbits;
    // delta
    unsigned v0 = (unsigned) -1;
    unsigned v1, dv;
    // golomb encoding
    int q = 0;
    unsigned r = 0;
    int rfill = 0;
    const unsigned rmask = (1 << m) - 1;
    // pending bits
    int n = 0;
    unsigned b = 0;
    unsigned bx;
  getq:
    w = *(unsigned short *) s;
    s += 2;
    b = word2bits[w];
    if (b < 0x1000) {
	n = 12;
	w = *(unsigned short *) s;
	bx = word2bits[w];
	if (bx < 0x1000) {
	    s += 2;
	    b |= (bx << 12);
	    n = 24;
	}
	goto putq;
    }
    switch (b & 0xf000) {
    case W_11:
	n = 11;
	b &= 0x0fff;
	goto putq;
    case W_10:
	n = 10;
	b &= 0x0fff;
	goto putq;
    case W_06:
	assert(!"final 6q char");
	return -1;
    case W_05:
	assert(!"final 5q char");
	return -1;
    case W_00:
	if (q > 5) {
	    assert(!"q overrun");
	    return -1;
	}
	return v - v_start;
    default:
	assert(!"bad word");
	return -1;
    }
  getr:
    w = *(unsigned short *) s;
    s += 2;
    b = word2bits[w];
    if (b < 0x1000) {
	n = 12;
	w = *(unsigned short *) s;
	bx = word2bits[w];
	if (bx < 0x1000) {
	    s += 2;
	    b |= (bx << 12);
	    n = 24;
	}
	goto putr;
    }
    switch (b & 0xf000) {
    case W_11:
	n = 11;
	b &= 0x0fff;
	goto putr;
    case W_10:
	n = 10;
	b &= 0x0fff;
	goto putr;
    case W_06:
	n = 6;
	b &= 0x0fff;
	// oh-oh
	r |= (b << rfill);
	rfill += n;
	left = rfill - m;
	if (left < 0)
	    assert(!"not enugh left");
	r &= rmask;
	dv = (q << m) | r;
	v1 = v0 + dv + 1;
	*v++ = v1;
	v0 = v1;
	q = 0;
	b >>= n - left;
	n = left;
	assert(b == 0);
	return v - v_start;
    case W_05:
	n = 5;
	b &= 0x0fff;
	// oh-oh
	r |= (b << rfill);
	rfill += n;
	left = rfill - m;
	if (left < 0)
	    assert(!"not enugh left");
	r &= rmask;
	dv = (q << m) | r;
	v1 = v0 + dv + 1;
	*v++ = v1;
	v0 = v1;
	q = 0;
	b >>= n - left;
	n = left;
	assert(b == 0);
	return v - v_start;
    case W_00:
	assert(!"incomlete r");
	return v - v_start;
    default:
	assert(!"bad word");
	return -1;
    }
  putr:
    r |= (b << rfill);
    rfill += n;
  rchk:
    left = rfill - m;
    if (left < 0)
	goto getr;
    r &= rmask;
    dv = (q << m) | r;
    v1 = v0 + dv + 1;
    *v++ = v1;
    v0 = v1;
    q = 0;
    b >>= n - left;
    n = left;
  putq:
    if (b == 0) {
	q += n;
	goto getq;
    }
    vbits = __builtin_ffs(b);
    n -= vbits;
    b >>= vbits;
    q += vbits - 1;
    r = b;
    rfill = n;
    // rchk
    left = rfill - m;
    if (left < 0)
	goto getr;
    r &= rmask;
    dv = (q << m) | r;
    v1 = v0 + dv + 1;
    *v++ = v1;
    v0 = v1;
    q = 0;
    b >>= n - left;
    n = left;
    // putq
    if (b == 0) {
	q += n;
	goto getq;
    }
    vbits = __builtin_ffs(b);
    n -= vbits;
    b >>= vbits;
    q += vbits - 1;
    r = b;
    rfill = n;
    // rchk
    left = rfill - m;
    if (left < 0)
	goto getr;
    r &= rmask;
    dv = (q << m) | r;
    v1 = v0 + dv + 1;
    *v++ = v1;
    v0 = v1;
    q = 0;
    b >>= n - left;
    n = left;
    // putq
    if (b == 0) {
	q += n;
	goto getq;
    }
    vbits = __builtin_ffs(b);
    n -= vbits;
    b >>= vbits;
    q += vbits - 1;
    r = b;
    rfill = n;
    goto rchk;
}

// ex: set ts=8 sts=4 sw=4 noet:
