/* Need %as for scanf */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rpmsetcmp.h"

static int setcmp(const char *s1, const char *s2)
{
    if (strncmp(s1, "set:", 4) == 0)
	s1 += 4;
    if (strncmp(s2, "set:", 4) == 0)
	s2 += 4;
    int cmp = rpmsetcmp(s1, s2);
    switch (cmp) {
    case 1:
    case 0:
    case -1:
    case -2:
    case -3:
	printf("%d\n", cmp);
	return 0;
    case -11:
	fprintf(stderr, "%s: set1 error\n", __FILE__);
	break;
    case -12:
	fprintf(stderr, "%s: set2 error\n", __FILE__);
	break;
    default:
	fprintf(stderr, "%s: unknown error\n", __FILE__);
	break;
    }
    return 1;
}

int main(int argc, const char **argv)
{
    assert(argc == 1 || argc == 3);
    if (argc == 3)
	return setcmp(argv[1], argv[2]);
    int rc = 0;
    while (1) {
	char *s1 = NULL, *s2 = NULL;
	int n = scanf("%as %as", &s1, &s2);
	if (n == EOF)
	    break;
	assert(n == 2);
	assert(s1 && s2);
	rc |= setcmp(s1, s2);
	free(s1);
	free(s2);
    }
    return rc;
}

/* ex: set ts=8 sts=4 sw=4 noet: */
