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
    long c;
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
    c = (unsigned char) *s++;
    b = char2bits[c];
    if (b < 62) {
	if (b) {
	    vbits = __builtin_ffs(b);
	    q += vbits - 1;
	    r = b >> vbits;
	    rfill = 6 - vbits;
	    goto getr;
	}
	else {
	    q += 6;
	    goto getq;
	}
    }
    switch (b) {
    case 62:
	q += 1;
	r = 7;
	rfill = 3;
	goto getr;
    case 63:
	r = 15;
	rfill = 4;
	goto getr;
    case 0xff:
	// end of line
	if (q > 5)
	    return -2;
	return v - v_start;
    default:
	// unknown character
	return -1;
    }
  getr:
    c = (unsigned char) *s++;
    b = char2bits[c];
    if (b < 62) {
	n = 6;
#if 1
	c = (unsigned char) *s;
	bx = char2bits[c];
	if (bx < 62) {
	    s++;
	    b |= (bx << 6);
	    n = 12;
	    c = (unsigned char) *s;
	    bx = char2bits[c];
	    if (bx < 62) {
		s++;
		b |= (bx << 12);
		n = 18;
		c = (unsigned char) *s;
		bx = char2bits[c];
		if (bx < 62) {
		    s++;
		    b |= (bx << 18);
		    n = 24;
		}
	    }
	}
#endif
#if 1
    r |= (b << rfill);
    rfill += n;

    // rchk again
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
    // putq again:
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
    // rchk again
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
    // putq again:
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
    // rchk again
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
    // putq again:
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
#endif
	goto getr;
    }
    if (b == 0xff)
	goto eolr;
    if (b == 0xee)
	return -1;
    n = 5;
    b &= 31;
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
  //putq:
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
    // rchk again
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
    // putq again:
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
    // rchk again
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
    // putq again:
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
    // rchk again
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
    // putq again:
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
    // and again:
    goto rchk;
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
    goto getr;
  eolr:
    return -1;
}

// ex: set ts=8 sts=4 sw=4 noet:
