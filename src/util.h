#ifndef _UTIL_H
#define _UTIL_H

#include "config.h"

#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

#define xread1(f)     xread1l(f,fderr)
#define xread4(f)     xread4l(f,fderr)
#define xread8(f)     xread8l(f,fderr)
#define xread1l(f,l)  xreadn(f,l,u8)
#define xread4l(f,l)  xreadn(f,l,u32)
#define xread8l(f,l)  xreadn(f,l,u64)
#define xreadn(f,l,t) ({                              \
          t ret;                                      \
          xreadl(f, &ret, sizeof(t), l);              \
          ret;                                        \
        })
#define xread(f,b,n) xreadl(f,b,n,fderr)
#define xreadl(f,b,n,l) ({ if (fdread(f, b, n) < 0) goto l; })

int fdread(int fd, void *buf, size_t len);

/* calls *alloc and asserts that it succeeded */
void *xmalloc(size_t s);
void *xcalloc(size_t n, size_t s);
void *xrealloc(void *p, size_t s);

#endif /* _UTIL_H */
