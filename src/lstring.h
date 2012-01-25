#ifndef _LSTRING_H_
#define _LSTRING_H_

#include <stdlib.h>

#include "config.h"
#include "util.h"

typedef size_t lstr_idx;

typedef struct lstring {
  size_t  length;
  u32     hash;
  char    *ptr;
} lstring_t;

void lstr_init();
lstr_idx lstr_add(char *str, size_t size, int freeable);
lstring_t *lstr_get(lstr_idx index);

#endif /* _LSTRING_H_ */
