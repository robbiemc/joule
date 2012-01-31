/**
 * @file lhash.h
 * @brief Headers for the generic Lua hash table.
 */

#ifndef _LHASH_H
#define _LHASH_H

#include "luav.h"

#define LHASH_MAP_THRESH 80
#define LHASH_INIT_SIZE 10

/* Actual hash implementation, currently just a simple resizing table with
   linear probing to resolve collisions */
typedef struct lhash {
  u32 cap;        // capacity of the table
  u32 size;       // size of the table
  u32 length;     // max non-empty integer index of the table (for # operator)
  struct lh_pair {
    luav key;
    luav value;
  } *hash;        // hash table, array of key/value pairs
  struct lhash *metatable;   // the metatable for this table
  luav *metamethods;  // metamethods if this is a metatable
} lhash_t;

void lhash_init(lhash_t *map);
void lhash_free(lhash_t *map);
luav lhash_get(lhash_t *map, luav key);
void lhash_set(lhash_t *map, luav key, luav value);

#endif /* _LHASH_H */
