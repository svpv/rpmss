#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rpmset.h"

int main(int argc, const char **argv)
{
    assert(argc == 2);
    int bpp = atoi(argv[1]);
    assert(bpp >= 7);
    assert(bpp <= 32);
    struct rpmset *set = rpmsetNew();
    char *line = NULL;
    size_t alloc_size = 0;
    ssize_t len;
    int added = 0;
    while ((len = getline(&line, &alloc_size, stdin)) >= 0) {
	if (len > 0 && line[len-1] == '\n')
	    line[--len] = '\0';
	if (len == 0)
	    continue;
	rpmsetAdd(set, line);
	added++;
    }
    assert(added > 0);
    char *str = rpmsetFini(set, bpp);
    assert(str);
    printf("set:%s\n", str);
    free(str);
    return 0;
}
