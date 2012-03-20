#ifndef RPMSS_H
#define RPMSS_H

/** \ingroup rpmss
 * \file lib/rpmss.h
 * Set-string implementation routines.
 *
 * A set-string is an alphanumeric string which represents a set of numberic
 * values, such as hash values of some data elements.  For uniformly
 * distributed hash values, the encoding routine yields an optimal (shortest
 * length) string.  For exmaple, to encode a set of 1024 20-bit hash values,
 * it takes only about 11.55 bits, which is about 1.94 characters, per value
 * (the expected string length is 1988, which includes two leading characters
 * that encode parameters).  The corresponding limit set by information theory
 * is log_2{2^{20}\choose2^{10}}=11.44 bits per value.
 *
 * Written by Alexey Tourbin.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmss
 * Estimate the size of a string buffer for encoding.
 * @param c		number of values in a set
 * @param v		the values, sorted and unique
 * @param bpp		actual bits per value, 10..32
 * @return		buffer size for encoding, < 0 on error
 */
int rpmssEncodeSize(int c, unsigned *v, int bpp);

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
