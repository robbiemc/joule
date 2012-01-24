#ifndef _LSTRING_H_
#define _LSTRING_H_

#include <stdlib.h>

#include "config.h"
#include "util.h"

typedef struct lstring {
  size_t  length;
  u32     hash;
  char    *ptr;
} lstring_t;

size_t lstr_add(char *str, size_t size, int freeable);
lstring_t *lstr_get(size_t index);

#endif /* _LSTRING_H_ */
