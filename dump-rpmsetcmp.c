#include <stdio.h>
#include <assert.h>
#include <dlfcn.h>

int rpmsetcmp(const char *s1, const char *s2)
{
    static int (*next)(const char *s1, const char *s2);
    if (next == NULL) {
	next = dlsym(RTLD_NEXT, __func__);
	assert(next);
    }
    int ret = next(s1, s2);
    printf("%s\t%s\t%s\t%d\n", __func__, s1, s2, ret);
    return ret;
}
