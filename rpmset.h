#ifndef RPMSET_H_
#define RPMSET_H_

/*
 * API for creating set versions.
 */

// initialize new set
struct rpmset *rpmsetNew(void);

// add new symbol to set
void rpmsetAdd(struct rpmset *set, const char *sym);

// make set-version
char *rpmsetFini(struct rpmset *set, int bpp);

// free set
struct rpmset *rpmsetFree(struct rpmset *set);

#endif
