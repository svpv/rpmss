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
} __attribute__((aligned(16)));

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#define xmalloc malloc

#ifdef NOSSE2
#undef __SSE2__
#endif

#ifdef __SSE2__
#include <emmintrin.h>
#include <strings.h>
#endif

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
    // Find hash
#if defined(__SSE2__)
    const unsigned key[4] __attribute__((aligned(16))) = { hash, hash, hash, hash };
    const __m128i xmm1 = _mm_load_si128((__m128i *) &key);
    __m128i xmm2, xmm3;
    unsigned short mask;
#define FINDHASH(LOAD)				\
    do {					\
	xmm2 = LOAD((__m128i *) hp);		\
	xmm3 = _mm_cmpeq_epi32(xmm1, xmm2);	\
	mask = _mm_movemask_epi8(xmm3);		\
	if (mask) {				\
	    hp += ffs(mask) >> 2;		\
	    goto foundhash;			\
	}					\
	hp += 4;				\
    } while (0)
#define FASTFIND()				\
    do {					\
	FINDHASH(_mm_load_si128);		\
	FINDHASH(_mm_load_si128);		\
	FINDHASH(_mm_load_si128);		\
	FINDHASH(_mm_load_si128);		\
    } while (0)
    // Fast aligned loads, fully unrolled
    FASTFIND();
    FASTFIND();
    FASTFIND();
    while (1) {
	// Unaligned loads, the loop can be reentered
	while (1) {
	    // Three iterations are optimal here
	    FINDHASH(_mm_loadu_si128);
	    FINDHASH(_mm_loadu_si128);
	    FINDHASH(_mm_loadu_si128);
	}
    foundhash:
#else	// brackets balanced below
    while (1) {
	while (1) {
	    // Cf. Quicker sequential search in [Knuth, Vol.3, p.398]
	    if (hp[0] == hash) break;
	    if (hp[1] == hash) { hp += 1; break; }
	    if (hp[2] == hash) { hp += 2; break; }
	    if (hp[3] == hash) { hp += 3; break; }
	    hp += 4;
	}
#endif
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
#ifdef __SSE2__
    }
#else	// brackets balanced here
    }
#endif
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
