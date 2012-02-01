#ifndef _UTIL_H
#define _UTIL_H

#include "config.h"

#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

u8   pread1(u8 **p);
u32  pread4(u8 **p);
u64  pread8(u8 **p);

/* calls *alloc and asserts that it succeeded */
void *xmalloc(size_t s);
void *xcalloc(size_t n, size_t s);
void *xrealloc(void *p, size_t s);

#endif /* _UTIL_H */
