#ifndef _LSTRING_H_
#define _LSTRING_H_

#include <stdlib.h>
#include "util.h"

#define SKIP_STRING(ptr) ((u8*)(ptr) + ((lstring_t*)(ptr))->length + sizeof(size_t))

typedef struct lstring {
  size_t  length;
  char    data;
} __attribute__((packed)) lstring_t;

#endif /* _LSTRING_H_ */
