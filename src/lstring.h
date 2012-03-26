#ifndef _LSTRING_H_
#define _LSTRING_H_

#include <stdlib.h>

#include "config.h"
#include "util.h"

typedef struct lstring {
  size_t  length;
  u32     hash;
  u32     permanent;
  char    data[1];
} lstring_t;

#define LSTR(s) lv_string(lstr_literal(s, TRUE))

lstring_t *lstr_alloc(size_t size);
lstring_t *lstr_realloc(lstring_t *str, size_t size);
lstring_t *lstr_add(lstring_t *str);
void       lstr_remove(lstring_t *str);
lstring_t *lstr_literal(char *cstr, int keep);
int lstr_compare(lstring_t *s1, lstring_t *s2);

lstring_t *lstr_empty();

void lstr_gc();

#endif /* _LSTRING_H_ */
