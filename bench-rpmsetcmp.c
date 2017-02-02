struct two {
    const char *s1;
    const char *s2;
};

#define MAXTWOS (1<<20)
static struct two twos[MAXTWOS];
static int ntwos;

#include <string.h>
#include <assert.h>

static void dotwo(const char *s1, const char *s2)
{
    if (strncmp(s1, "set:", 4) == 0) s1 += 4;
    if (strncmp(s2, "set:", 4) == 0) s2 += 4;
    twos[ntwos++] = (struct two) { s1, s2 };
    assert(ntwos <= MAXTWOS);
}

#include <stdio.h>

static void readlines(void)
{
    char *line = NULL;
    size_t alloc_size = 0;
    ssize_t len;
    while ((len = getline(&line, &alloc_size, stdin)) >= 0) {
	if (len > 0 && line[len-1] == '\n')
	    line[--len] = '\0';
	char *s1 = line;
	char *s2 = strchr(line, ' ');
	if (s2 == NULL)
	    s2 = strchr(line, '\t');
	assert(s2);
	*s2++ = '\0';
	dotwo(s1, s2);
	line = NULL;
	alloc_size = 0;
    }
}

#include "rpmsetcmp.h"

static void setcmp(void)
{
    for (int i = 0; i < ntwos; i++) {
	struct two *two = twos + i;
	int ret = rpmsetcmp(two->s1, two->s2);
	assert(ret >= -2);
    }
}

#include "bench.h"

int main()
{
    readlines();
    BENCH(setcmp);
    return 0;
}
