void bench(void (*func)(void), const char *name);
#define BENCH(func) bench(func, #func)
