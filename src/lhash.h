/**
 * @file lhash.h
 * @brief Headers for the generic Lua hash table.
 */

#ifndef _LHASH_H
#define _LHASH_H

#include "luav.h"

#define LHASH_MAP_THRESH 60
#define LHASH_INIT_TSIZE 17
#define LHASH_INIT_ASIZE 10

#define LHASH_RESIZING  (1 << 0)
#define LHASH_ITERATING (1 << 1)

/* Actual hash implementation, currently just a simple resizing table with
   linear probing to resolve collisions */
typedef struct lhash {
  int flags;       // is the map currently being resized?
  u32 tcap;        // capacity of the table
  u32 tsize;       // size of the table
  size_t length;   // max non-empty integer index of the table (for # operator)
  struct lh_pair {
    luav key;
    luav value;
  } *table;        // hash table, array of key/value pairs

  u32  acap;       // array capacity
  u32  asize;      // array size (# of elements in array)
  luav *array;     // array part which acts like an array

  struct lhash *metatable;   // the metatable for this table
} lhash_t;

typedef int(lcomparator_t)(luav*, luav*);

lhash_t* lhash_alloc(void);
lhash_t* lhash_hint(u32 arr_size, u32 table_size);
void lhash_init(lhash_t *map, u32 arr_size, u32 table_size);
luav lhash_get(lhash_t *map, luav key);
void lhash_set(lhash_t *map, luav key, luav value);

void   lhash_next(lhash_t *map, luav key, luav *nxtkey, luav *nxtval);
double lhash_maxn(lhash_t *map);
void   lhash_insert(lhash_t *map, u32 pos, luav value);
luav   lhash_remove(lhash_t *map, u32 pos);
void   lhash_sort(lhash_t *map, lcomparator_t *comp);
void   lhash_array(lhash_t *map, luav *base, u32 amt);

#endif /* _LHASH_H */
