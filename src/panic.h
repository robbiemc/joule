#ifndef _PANIC_H
#define _PANIC_H

#include <stdio.h>

#define panic(fmt, ...) {                                                     \
    fprintf(stderr, "[panic %s at %d] - " fmt "\n",                           \
            __FILE__, __LINE__, ## __VA_ARGS__);                              \
    abort();                                                                  \
  }

#endif /* _PANIC_H */
