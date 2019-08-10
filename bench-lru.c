#include <stdint.h>

#define CACHE_SIZE (256 - 1)
#define MIDPOINT (CACHE_SIZE * 7 / 8)
#define MOVSTEP 32

struct cache_ent {
    unsigned fullhash;
    int len;
    int n;
};

struct cache {
    uint16_t hv[CACHE_SIZE + 1];
    int hc;
    int hit, miss;
    struct cache_ent *ev[CACHE_SIZE];
};

#include <string.h>
#include <stdlib.h>
#define xmalloc malloc

static inline unsigned hash16(const char *str, unsigned len)
{
    uint32_t h;
    memcpy(&h, str + 4, 4);
    h *= 2654435761U;
    h += len << 16;
    return h >> 16;
}

#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

static int cache_decode(struct cache *c,
			const char *str, int len,
			unsigned fullhash,
			int n /* expected v[] size */,
			const unsigned **pv)
{
    int i;
    struct cache_ent *ent;
    uint16_t *hv = c->hv;
    struct cache_ent **ev = c->ev;
    unsigned hash = hash16(str, len);
#if defined(__SSE2__)
    __m128i xmm0 = _mm_set1_epi16(hash);
#elif defined(__ARM_NEON) || defined(__aarch64__)
    uint16x8_t xmm0 = vdupq_n_u16(hash);
#endif
    // Install sentinel
    hv[c->hc] = hash;
    uint16_t *hp = hv;
    while (1) {
	// Find hash
#if defined(__SSE2__)
	unsigned mask;
	do {
	    __m128i xmm1 = _mm_loadu_si128((void *)(hp + 0));
	    __m128i xmm2 = _mm_loadu_si128((void *)(hp + 8));
	    hp += 16;
	    xmm1 = _mm_cmpeq_epi16(xmm1, xmm0);
	    xmm2 = _mm_cmpeq_epi16(xmm2, xmm0);
	    xmm1 = _mm_packs_epi16(xmm1, xmm2);
	    mask = _mm_movemask_epi8(xmm1);
	} while (mask == 0);
	hp -= 16;
	hp += __builtin_ctz(mask);
#elif defined(__ARM_NEON) || defined(__aarch64__)
	uint64_t mask;
	uint16x8_t xmm1 = vld1q_u16(hp + 0);
	uint16x8_t xmm2 = vld1q_u16(hp + 8);
	xmm1 = vceqq_u16(xmm1, xmm0);
	xmm2 = vceqq_u16(xmm2, xmm0);
	do {
	    uint8x16_t maskv = vcombine_u8(vqmovn_u16(xmm1), vqmovn_u16(xmm2));
	    uint8x8_t maskw = vshrn_n_u16(vreinterpretq_u16_u8(maskv), 4);
	    mask = vget_lane_u64(vreinterpret_u64_u8(maskw), 0);
	    // Moving the mask takes a while, start another iteration.
	    xmm1 = vld1q_u16(hp + 16);
	    xmm2 = vld1q_u16(hp + 24);
	    hp += 16;
	    xmm1 = vceqq_u16(xmm1, xmm0);
	    xmm2 = vceqq_u16(xmm2, xmm0);
	} while (mask == 0);
	hp -= 16;
	hp += __builtin_ctzll(mask) / 4;
#else
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
	if (i > MOVSTEP) {
	    hv += (unsigned) i - MOVSTEP;
	    ev += (unsigned) i - MOVSTEP;
	    memmove(hv + 1, hv, MOVSTEP * sizeof hv[0]);
	    memmove(ev + 1, ev, MOVSTEP * sizeof ev[0]);
	    hv[0] = hash;
	    ev[0] = ent;
	}
	*pv = NULL;
	return ent->n;
    }
    // decode
    ent = xmalloc(sizeof(*ent));
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
    *pv = NULL;
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
    if (len < 512) // approximates DECODE_CACHE_SIZE = 256
	return 0;
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
