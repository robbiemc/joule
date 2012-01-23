#ifndef _LSTRING_H_
#define _LSTRING_H_

#include <stdlib.h>

#include "config.h"
#include "util.h"

#define SKIP_STRING(ptr) ((u8*)(ptr) + ((lstring_t*)(ptr))->length + sizeof(size_t))

#define LSTRING_EMBEDDED(str) ((str)->length)

typedef struct lstring {
  size_t  length;
  union {
    char *ptr;
    char bytes[MAX_STRING_EMBED_SIZE];
  } data;
} __attribute__((packed)) lstring_t;

#endif /* _LSTRING_H_ */
