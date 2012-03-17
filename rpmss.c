#include <stdio.h>
#include <assert.h>
#include "rpmss.h"

#define fprintf(fp, args...) (void)(fp)

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

// This table maps alnum characters to their numeric values.
static
const int char2bits[256] = {
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
    fprintf(stderr, "bpp=%d m=%d\n", bpp, m);
    *pbpp = bpp;
    const unsigned *v_start = v;
    enum { ST_VLEN, ST_MBITS } state = ST_VLEN;
    unsigned r = 0;
    int rfill = 0;
    int q = 0;
    unsigned v0 = (unsigned) -1;
    unsigned v1, dv;
    inline
    void putNbits(unsigned c, int n)
    {
       if (state == ST_VLEN)
           goto vlen;
       r |= (c << rfill);
       rfill += n;
    rcheck: ;
       int left = rfill - m;
       if (left < 0)
           return;
       r &= (1 << m) - 1;
       dv = (q << m) | r;
       v1 = v0 + dv + 1;
       *v++ = v1;
       v0 = v1;
	fprintf(stderr, "q=%d r=%u\n", q, r);
       fprintf(stderr, "v1=%u dv=%u\n", v1, dv);
       q = 0;
       state = ST_VLEN;
       c >>= n - left;
       n = left;
       fprintf(stderr, "left=%d %u\n", left, c);
    vlen:
       if (c == 0) {
           q += n;
           return;
       }
       int vbits = __builtin_ffs(c);
       n -= vbits;
       c >>= vbits;
       q += vbits - 1;
       r = c;
       rfill = n;
       state = ST_MBITS;
       goto rcheck;
    }
    inline void put5bits(unsigned c) { putNbits(c, 5); }
    inline void put6bits(unsigned c) { putNbits(c, 6); }
    while (1) {
	long c = (unsigned char) *s++;
	unsigned bits = char2bits[c];
	while (bits < 62) {
	    fprintf(stderr, "put6bits: %u\n", bits);
	    put6bits(bits);
	    c = (unsigned char) *s++;
	    bits = char2bits[c];
	}
	if (bits == 0xff)
	    break;
	if (bits == 0xee)
	    return -1;
	put5bits(bits & 31);
    }
    return v - v_start;
}

// ex: set ts=8 sts=4 sw=4 noet:
