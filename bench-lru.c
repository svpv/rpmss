#define CACHE_SIZE (256 - 2)
#define MIDPOINT (CACHE_SIZE * 7 / 8)

struct cache_ent {
    unsigned fullhash;
    int len;
    int n;
    unsigned v[];
};

struct cache {
    unsigned hv[CACHE_SIZE + 1];
    int hc;
    int hit, miss;
    struct cache_ent *ev[CACHE_SIZE];
};

#include <string.h>
#include <stdlib.h>
#define xmalloc malloc

static int cache_decode(struct cache *c,
			const char *str, int len,
			unsigned fullhash,
			int n /* expected v[] size */,
			const unsigned **pv)
{
    int i;
    struct cache_ent *ent;
    unsigned *hv = c->hv;
    struct cache_ent **ev = c->ev;
    unsigned hash;
    memcpy(&hash, str, sizeof hash);
    hash ^= len;
    // Install sentinel
    hv[c->hc] = hash;
    unsigned *hp = hv;
    while (1) {
	// Find hash
	while (1) {
	    // Cf. Quicker sequential search in [Knuth, Vol.3, p.398]
	    if (hp[0] == hash) break;
	    if (hp[1] == hash) { hp += 1; break; }
	    if (hp[2] == hash) { hp += 2; break; }
	    if (hp[3] == hash) { hp += 3; break; }
	    hp += 4;
	}
	i = hp - hv;
	// Found sentinel?
	if (i == c->hc)
	    break;
	// Found an entry
	ent = ev[i];
	// Recheck the entry
	if (len != ent->len || fullhash != ent->fullhash) {
	    hp++;
	    continue;
	}
	// Hit, move to front
	c->hit++;
	if (i) {
	    memmove(hv + 1, hv, i * sizeof hv[0]);
	    memmove(ev + 1, ev, i * sizeof ev[0]);
	    hv[0] = hash;
	    ev[0] = ent;
	}
	*pv = ent->v;
	return ent->n;
    }
    // decode
#define SENTINELS 1
    ent = xmalloc(sizeof(*ent) + (n + SENTINELS) * sizeof(unsigned));
    unsigned *v = ent->v;
    for (i = 0; i < n + SENTINELS; i++)
    	v[i] = i;
    ent->fullhash = fullhash;
    ent->len = len;
    ent->n = n;
    c->miss++;
    // insert
    if (c->hc <= MIDPOINT)
	i = c->hc++;
    else {
	// free last entry
	if (c->hc < CACHE_SIZE)
	    c->hc++;
	else
	    free(ev[CACHE_SIZE - 1]);
	// position at the midpoint
	i = MIDPOINT;
	memmove(hv + i + 1, hv + i, (CACHE_SIZE - i - 1) * sizeof hv[0]);
	memmove(ev + i + 1, ev + i, (CACHE_SIZE - i - 1) * sizeof ev[0]);
    }
    hv[i] = hash;
    ev[i] = ent;
    *pv = v;
    return n;
}

struct line {
    char str[4];
    int len;
    unsigned fullhash;
};

#define MAXLINES (1<<20)
static struct line lines[MAXLINES];
static int nlines;

static unsigned int jhash(const char *str)
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

#include <stdbool.h>

static bool doline(const char *line, size_t len)
{
    unsigned hash = jhash(line);
    char s[4];
    strncpy(s, line, 4);
    lines[nlines++] = (struct line) { { s[0], s[1], s[2], s[3] }, len, hash };
    return nlines == MAXLINES;
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
	if (len == 0)
	    continue;
	if (doline(line, len))
	    break;
    }
    free(line);
}

static struct cache C;
static volatile unsigned ret;

static void lru(void)
{
    C.hc = C.hit = C.miss = 0;
    for (int i = 0; i < nlines; i++) {
	struct line *l = lines + i;
	const unsigned *v;
	int n = cache_decode(&C, l->str, l->len, l->fullhash, 3, &v);
	ret += n;
    }
}

#include "bench.h"

int main()
{
    readlines();
    BENCH(lru);
    printf("%.2f%% hit ratio\n", 100.0 * C.hit / (C.hit + C.miss));
    return 0;
}
