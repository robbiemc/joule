#ifndef _LSTRING_H_
#define _LSTRING_H_

#include <stdlib.h>

#include "config.h"
#include "util.h"

typedef struct lstring {
  size_t  length;
  u32     hash;
  u32     type;
  char    data[1];
} lstring_t;

#define LSTR_MALLOC 0
#define LSTR_GC     1

#define LSTR(s) lv_string(lstr_literal(s))

lstring_t *lstr_alloc(size_t size);
lstring_t *lstr_realloc(lstring_t *str, size_t size);
lstring_t *lstr_add(lstring_t *str);
void       lstr_remove(lstring_t *str);
lstring_t *lstr_literal(char *cstr);

lstring_t *lstr_empty();

void lstr_gc();

#endif /* _LSTRING_H_ */
