#ifndef _HASH_H
#define _HASH_H

#include "luav.h"

#define LHASH_MAP_THRESH 80
#define LHASH_INIT_SIZE 10

typedef struct lhash lhash_t;

lhash_t *lhash_alloc(void);
void lhash_free(lhash_t *hash);
luav lhash_get(lhash_t *hash, luav key);
void lhash_set(lhash_t *hash, luav key, luav value);

#endif /* _HASH_H */
