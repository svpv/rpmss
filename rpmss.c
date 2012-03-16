#include <stdio.h>
#include <assert.h>
#include "rpmss.h"

//#define fprintf(fmt, args...) (void)(fmt);

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
    if (c < 1)
	return -1;
    if (bpp < 10 || bpp > 32)
	return -2;
    if (bpp < 32 && v[c - 1] >> bpp)
	return -3;
    int m = bpp - log2i(c) - 1;
    if (m < 7)
	m = 7;
    const char *s_start = s;
    *s++ = bpp - 7 + 'a';
    *s++ = m - 7 + 'a';
    const unsigned mask = (1 << m) - 1;
    const unsigned *v_end = v + c;
    unsigned v0 = (unsigned) -1;
    unsigned v1, dv;
    int q;
    unsigned r;
    int n = 0;
    unsigned b = 0;
#define PUTBITS \
    switch (b & 63) { \
    case 60: \
    case 61: \
	fprintf(stderr, "put60 -> %c\n", bits2char[60]); \
	*s++ = bits2char[60]; \
	b >>= 5; \
	n -= 5; \
	fprintf(stderr, "left %d %u\n", n, b);\
	break; \
    case 62: \
    case 63: \
	fprintf(stderr, "put61 -> %c\n", bits2char[61]); \
	*s++ = bits2char[61]; \
	b >>= 5; \
	n -= 5; \
	fprintf(stderr, "left %d %u\n", n, b);\
	break; \
    default: \
	fprintf(stderr, "b=%u\n", b & 63); \
	assert((b & 63) < 60); \
	fprintf(stderr, "put-normal %u -> %c\n", b & 63, bits2char[b & 63]); \
	*s++ = bits2char[b & 63]; \
	b >>= 6; \
	n -= 6; \
	fprintf(stderr, "left %d %u\n", n, b);\
	break; \
    } 
    //
    do {
	assert(n < 6);
	// dv
	v1 = *v++;
	if (v1 < v0 + 1)
	    return -4;
	dv = v1 - (v0 + 1);
	v0 = v1;
	// q
	q = dv >> m;
	r = dv & mask;
	fprintf(stderr, "q=%d r=%u\n", q, r);
	fprintf(stderr, "v1=%u dv=%u\n", v1, dv);
	n += q;
	while (n >= 6)
	    PUTBITS;
	b |= (1 << n);
	n++;
	if (n >= 6)
	    PUTBITS;
	// r
	r = dv & mask;
	b |= (r << n);
	n += m;
	assert(n <= 32);
	while (n >= 6)
	    PUTBITS;
    }
    while (v < v_end);
    assert(n < 6);
    if (n)
	PUTBITS;
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
    ['y'] = 60,
    ['z'] = 61,
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
	while (bits < 60) {
	    fprintf(stderr, "put6bits: %u\n", bits);
	    put6bits(bits);
	    c = (unsigned char) *s++;
	    bits = char2bits[c];
	}
	if (bits == 0xff)
	    break;
	if (bits == 0xee)
	    return -1;
	if (bits == 60) {
	    fprintf(stderr, "put5bits: %u\n", 60);
	    put5bits(30);
	}
	else {
	    fprintf(stderr, "put5bits: %u\n", 61);
	    assert(bits == 61);
	    put5bits(31);
	}
    }
    return v - v_start;
}

// ex: set ts=8 sts=4 sw=4 noet:
