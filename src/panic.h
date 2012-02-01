#ifndef _PANIC_H
#define _PANIC_H

#include <stdio.h>

#define panic(fmt, ...) {                                                     \
    fprintf(stderr, "%s:%d - " fmt "\n",                                      \
            __FILE__, __LINE__, ## __VA_ARGS__);                              \
    abort();                                                                  \
  }

#define xassert(e) {                                     \
    if (!(e)) { panic("failed assertion: %s", #e); }     \
  }

#endif /* _PANIC_H */
