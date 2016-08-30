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
 * that encode parameters).  The corresponding limit set by the information
 * theory is log_2{2^{20}\choose2^{10}}=11.44 bits per value.
 *
 * The implementation provides optimized encoding and decoding routines.
 *
 * Written by Alexey Tourbin.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize encoding; estimate string buffer size.
 * @param v		the values, sorted and unique
 * @param n		number of values
 * @param bpp		actual bits per value, 7..32
 * @return		output buffer size (incl. '\0'), < 0 on error
 */
int rpmssEncodeInit(const unsigned *v, int n, int bpp);

/**
 * Squeeze a set of numeric values into a set-string.
 * @param v		the values, sorted and unique
 * @param n		number of values
 * @param bpp		actual bits per value, 7..32
 * @retval s		alnum output, null-terminated on success
 * @return		output string length, < 0 on error
 */
int rpmssEncode(const unsigned *v, int n, int bpp, char *s);

/**
 * Initialize decoding; estimate the number of values in a set.
 * @param s		alnum string to decode, null-terminated
 * @param len		alnum string length, i.e. strlen(s)
 * @retval pbpp		original bits per value
 * @return		number of values (upper bound), < 0 on error
 */
int rpmssDecodeInit(const char *s, int len, int *pbpp);

/**
 * Bring back the set of numeric values out of a set-string.
 * @param s		set-string to decode, null-terminated
 * @retval v		decoded values, sorted and unique
 * @return		number of values, < 0 on error
 */
int rpmssDecode(const char *s, unsigned *v);

#ifdef __cplusplus
}
#endif

#endif
