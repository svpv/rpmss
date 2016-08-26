#include <assert.h>
#include "rpmss.h"

static bad_set(const unsigned *v, int n, int bpp)
{
    int len = rpmssEncodeInit(v, n, bpp);
    assert(len > 0);
    char s[len];
    len = rpmssEncode(v, n, bpp, s);
    assert(len < 0);
}

#define BAD_SET(bpp, ...) \
    { \
	const unsigned v[] = { __VA_ARGS__ }; \
	bad_set(v, sizeof(v)/sizeof*v, bpp); \
    } 

int main(int argc, char **argv)
{
    /* The sum 1+2 overflows but is smaller than 3 */
    BAD_SET(32, 0xff, 0xffffffff, 0xffff, 0xffffffff);
    return 0;
}

/* ex: set ts=8 sts=4 sw=4 noet: */
