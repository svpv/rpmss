#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include "bench.h"

#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#define rdtsc() __rdtsc()
#elif defined(__aarch64__)
static inline uint64_t rdtsc(void)
{
    uint64_t t;
    asm volatile("mrs %0, cntvct_el0" : "=r"(t));
    return t;
}
#else
#error "rdtsc not supported"
#endif

static __attribute__((noinline))
uint64_t time1(void (*func)(void), bool u)
{
    if (u)
	usleep(1);
    uint64_t begin = rdtsc();
    func();
    uint64_t end = rdtsc();
    return end - begin;
}

static uint64_t avg4(uint64_t v1, uint64_t v2,
		     uint64_t v3, uint64_t v4)
{
    uint64_t sum = v1 + v2 + v3 + v4;
    uint64_t min = v1, max = v1;
    if (v2 < min) min = v2; else if (v2 > max) max = v2;
    if (v3 < min) min = v3; else if (v3 > max) max = v3;
    if (v4 < min) min = v4; else if (v4 > max) max = v4;
    sum -= min + max;
    return sum / 2;
}

void bench(void (*func)(void), const char *name)
{
    uint64_t t01 = time1(func, 1);
    uint64_t t02 = time1(func, 1);
    uint64_t t03 = time1(func, 1);
    uint64_t t04 = time1(func, 1);
    asm volatile ("" ::: "memory");
    uint64_t t11 = time1(func, 0);
    uint64_t t12 = time1(func, 0);
    uint64_t t13 = time1(func, 0);
    uint64_t t14 = time1(func, 0);
    asm volatile ("" ::: "memory");
    printf("%-16s\t%12" PRIu64 " cold\t%12" PRIu64 " hot\n", name,
	   avg4(t01, t02, t03, t04),
	   avg4(t11, t12, t13, t14));
}
