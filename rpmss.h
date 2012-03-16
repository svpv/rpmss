#ifndef RPMSS_H
#define RPMSS_H

/** \ingroup rpmss
 * \file lib/rpmss.h
 * Set-string implementation routines.
 *
 * Written by Alexey Tourbin.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmss
 * Estimate size
 * @param c		number of values in a set
 * @param bpp		bits per value
 * @return		buffer size for encoding, < 0 on error
 */
int rpmssEncodeSize(int c, int bpp);

/** \ingroup rpmss
 * @param c		number of values in a set
 * @param v		the values, sorted and unique
 * @param bpp		actual bits per value, 10..32
 * @param s		alnum output, null-terminated on success
 * @return		alnum string length, < 0 on error
 */
int rpmssEncode(int c, const unsigned *v, int bpp, char *s);

int rpmssDecodeSize(const char *s, int len);
int rpmssDecode(const char *s, unsigned *v, int *pbpp);

#ifdef __cplusplus
}
#endif

#endif
