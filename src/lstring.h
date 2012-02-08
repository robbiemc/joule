#ifndef _LSTRING_H_
#define _LSTRING_H_

#include <stdlib.h>

#include "config.h"
#include "util.h"

typedef struct lstring {
  size_t  length;
  u32     hash;
  char    data[1];
} lstring_t;

#define LSTR(s) lv_string(lstr_literal(s))

lstring_t *lstr_alloc(size_t size);
lstring_t *lstr_add(lstring_t *str);
lstring_t *lstr_literal(char *cstr);

lstring_t *lstr_empty();

#endif /* _LSTRING_H_ */
