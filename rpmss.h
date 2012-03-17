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
 * Estimate the size of a string buffer for encoding.
 * @param c		number of values in a set
 * @param bpp		actual bits per value, 10..32
 * @return		buffer size for encoding, < 0 on error
 */
int rpmssEncodeSize(int c, int bpp);

/** \ingroup rpmss
 * Encode a set of numeric values into alnum string.
 * @param c		number of values in a set
 * @param v		the values, sorted and unique
 * @param bpp		actual bits per value, 10..32
 * @param s		alnum output, null-terminated on success
 * @return		alnum string length, < 0 on error
 */
int rpmssEncode(int c, const unsigned *v, int bpp, char *s);

/** \ingroup rpmss
 * 
 * @param s		alnum string to decode
 * @param len		alnum string length
 * @return		number of values, < 0 on error
 */
int rpmssDecodeSize(const char *s, int len);

/** \ingroup rpmss
 * 
 * @param s		alnum string to decode
 * @param len		alnum string length
 * @return		number of values, < 0 on error
 */
int rpmssDecode(const char *s, unsigned *v, int *pbpp);

#ifdef __cplusplus
}
#endif

#endif
