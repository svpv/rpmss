#ifndef RPMSS_H
#define RPMSS_H

/** \ingroup rpmss
 * \file lib/rpmss.h
 * Set-string implementation routines.
 *
 * A set-string is an alphanumeric string which represents a set of numeric
 * values, such as hash values of some data elements.  For uniformly
 * distributed hash values, the encoding routine yields an optimal (shortest
 * length) string.  For exmaple, to encode a set of 1024 20-bit hash values,
 * it takes only about 11.55 bits, which is about 1.94 characters, per value
 * (the expected string length is 1988, which includes two leading characters
 * that encode parameters).  The corresponding limit set by information theory
 * is log_2{2^{20}\choose2^{10}}=11.44 bits per value.
 *
 * Set-string format permits reasonably efficient decoding - e.g. no division
 * in a loop is required.  Furthermore, the decoding routine aims to provide
 * better performance by reading and processing two characters at a time.
 * However, a variable-length code requires additional validation steps, which
 * can become a somewhat limiting factor.  The decoding routine typically takes
 * about 25 instructions per character.  For comparison, a naive strlen
 * impelmentation takes 3 instructions per character.
 *
 * Written by Alexey Tourbin.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** \ingroup rpmss
 * Estimate the size of a string buffer for encoding.
 * @param v		the values, sorted and unique
 * @param n		number of values
 * @param bpp		actual bits per value, 8..32
 * @return		buffer size for encoding, < 0 on error
 */
int rpmssEncodeSize(const unsigned *v, int n, int bpp);

/** \ingroup rpmss
 * Encode a set of numeric values into alnum string.
 * @param v		the values, sorted and unique
 * @param n		number of values
 * @param bpp		actual bits per value, 8..32
 * @param s		alnum output, null-terminated on success
 * @return		alnum string length, < 0 on error
 */
int rpmssEncode(const unsigned *v, int n, int bpp, char *s);

/** \ingroup rpmss
 * 
 * @param s		alnum string to decode, null-terminated
 * @param len		alnum string length
 * @return		number of values (upper size), < 0 on error
 */
int rpmssDecodeSize(const char *s, int len);

/** \ingroup rpmss
 * 
 * @param s		alnum string to decode, null-terminated
 * @param len		alnum string length
 * @return		number of values, < 0 on error
 */
int rpmssDecode(const char *s, int len, unsigned *v, int *pbpp);

#ifdef __cplusplus
}
#endif

#endif
