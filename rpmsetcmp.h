#ifndef RPMSETCMP_H_
#define RPMSETCMP_H_

/*
 * Compare two set-versions.
 * @return
 *  1: set1  >  set2 (aka set2 \subset set1)
 *  0: set1 ==  set2
 * -1: set1  <  set2
 * -2: set1 !=  set2 (possibly with common elements)
 * -3: set1 !=  set2 (disjoint sets)
 * -11: set1 decoder error
 * -12: set2 decoder error
 * For performance reasons, set1 should come on behalf of Provides.
 */
int rpmsetcmp(const char *s1, const char *s2);

#endif
