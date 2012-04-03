#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rpmss.h"

// Internally, "struct set" is just a bag of strings and their hash values.
struct set {
    int c;
    struct sv {
	const char *s;
	unsigned v;
    } *sv;
};

struct set *set_new()
{
    struct set *set = malloc(sizeof *set);
    set->c = 0;
    set->sv = NULL;
    return set;
}

void set_add(struct set *set, const char *sym)
{
    const int delta = 1024;
    if ((set->c & (delta - 1)) == 0)
	set->sv = realloc(set->sv, sizeof(*set->sv) * (set->c + delta));
    set->sv[set->c].s = strdup(sym);
    set->sv[set->c].v = 0;
    set->c++;
}

// This routine does the whole job.
const char *set_fini(struct set *set, int bpp)
{
    if (set->c < 1)
	return NULL;
    if (bpp < 10)
	return NULL;
    if (bpp > 32)
	return NULL;
    unsigned mask = (bpp < 32) ? (1u << bpp) - 1 : ~0u;
    // Jenkins' one-at-a-time hash
    unsigned int hash(const char *str)
    {
	unsigned int hash = 0x9e3779b9;
	const unsigned char *p = (const unsigned char *) str;
	while (*p) {
	    hash += *p++;
	    hash += (hash << 10);
	    hash ^= (hash >> 6);
	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
    }
    // hash sv strings
    int i;
    for (i = 0; i < set->c; i++)
	set->sv[i].v = hash(set->sv[i].s) & mask;
    // sort by hash value
    int cmp(const void *arg1, const void *arg2)
    {
	struct sv *sv1 = (struct sv *) arg1;
	struct sv *sv2 = (struct sv *) arg2;
	if (sv1->v > sv2->v)
	    return 1;
	if (sv2->v > sv1->v)
	    return -1;
	return 0;
    }
    qsort(set->sv, set->c, sizeof *set->sv, cmp);
    // warn on hash collisions
    for (i = 0; i < set->c - 1; i++) {
	if (set->sv[i].v != set->sv[i+1].v)
	    continue;
	if (strcmp(set->sv[i].s, set->sv[i+1].s) == 0)
	    continue;
	fprintf(stderr, "warning: hash collision: %s %s\n",
		set->sv[i].s, set->sv[i+1].s);
    }
    // encode
    unsigned v[set->c];
    for (i = 0; i < set->c; i++)
	v[i] = set->sv[i].v;
    int uniqv(int c, unsigned *v)
    {
	int i, j;
	for (i = 0, j = 0; i < c; i++) {
	    while (i + 1 < c && v[i] == v[i+1])
		i++;
	    v[j++] = v[i];
	}
	return j;
    }
    int c = uniqv(set->c, v);
    // encode2
    char s[rpmssEncodeSize(v, c, bpp)];
    int len = rpmssEncode(v, c, bpp, s);
    if (len < 0)
	return NULL;
    return strdup(s);
}

int main(int argc, const char **argv)
{
    assert(argc == 2);
    int bpp = atoi(argv[1]);
    assert(bpp >= 10);
    assert(bpp <= 32);
    struct set *set = set_new();
    char *line = NULL;
    size_t alloc_size = 0;
    ssize_t len;
    int added = 0;
    while ((len = getline(&line, &alloc_size, stdin)) >= 0) {
	if (len > 0 && line[len-1] == '\n')
	    line[--len] = '\0';
	if (len == 0)
	    continue;
	set_add(set, line);
	added++;
    }
    assert(added > 0);
    const char *str = set_fini(set, bpp);
    assert(str);
    printf("set:%s\n", str);
    return 0;
}
