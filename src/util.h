#ifndef _UTIL_H
#define _UTIL_H

#include "config.h"

u8   pread1(u8 **p);
u32  pread4(u8 **p);
u64  pread8(u8 **p);

/* calls *alloc and asserts that it succeeded */
void *xmalloc(size_t s);
void *xcalloc(size_t n, size_t s);

#endif /* _UTIL_H */
