/**
 * @brief Hash table implementation
 *
 * Currently very simple, lots of room for optimization.
 *
 * TODO: optimize
 * TODO: shrink when keys set to nil
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "lhash.h"
#include "luav.h"
#include "util.h"

static void lhash_resize(lhash_t *hash);

/**
 * @brief Initialize a new hash table so that it is ready for use.
 *
 * A hash should always be initialized before use, but it should never be
 * initialized twice.
 *
 * @param map the hash to initialize
 */
void lhash_init(lhash_t *map) {
  int i;
  assert(map != NULL);
  map->cap = LHASH_INIT_SIZE;
  map->size = 0;
  map->length = 0;
  map->hash = xmalloc(LHASH_INIT_SIZE * sizeof(map->hash[0]));
  for (i = 0; i < LHASH_INIT_SIZE; i++) {
    map->hash[i].key = LUAV_NIL;
  }
}

/**
 * @brief Free all resources associated with the given hash
 *
 * Any hash initialized with lhash_init() should be deallocated with this
 * function, and this function should not be called more than once for a hash
 * table
 *
 * @param hash the hash to deallocate
 */
void lhash_free(lhash_t *hash) {
  free(hash->hash);
}

/**
 * @brief Fetch a key in a hash table
 *
 * @param map the table to fetch from
 * @param key the key to fetch
 * @return the value associated with the given key, NIL if the key is also NIL,
 *         or NIL if the key is not present in the table.
 */
luav lhash_get(lhash_t *map, luav key) {
  u32 i;
  assert(lv_gettype(key) != LUPVALUE);
  if (key == LUAV_NIL) {
    return LUAV_NIL;
  }
  u32 h = lv_hash(key);

  for (i = 0; ; i++) {
    u32 idx = (h + i) % map->cap;
    luav cur = map->hash[idx].key;
    if (cur == key) {
      return map->hash[idx].value;
    } else if (cur == LUAV_NIL) {
      return LUAV_NIL;
    }
  }
}

/**
 * @brief Set a value in a table for a specified key
 *
 * If the key is already present in the table, then the old value is removed in
 * favor of the current value. It is considered error to add a value for the
 * key NIL.
 *
 * @param map the table to alter
 * @param key the key to insert
 * @param value the corresponding value for the given key
 */
void lhash_set(lhash_t *map, luav key, luav value) {
  assert(key != LUAV_NIL);
  assert(lv_gettype(key) != LUPVALUE);
  assert(lv_gettype(value) != LUPVALUE);

  u32 i, h = lv_hash(key);
  for (i = 0; ; i++) {
    u32 idx = (h + i) % map->cap;
    luav cur = map->hash[idx].key;
    if (cur == key) {
      map->hash[idx].value = value;
      break;
    } else if (cur == LUAV_NIL) {
      map->hash[idx].key = key;
      map->hash[idx].value = value;
      map->size++;
      if (map->size * 100 / map->cap > LHASH_MAP_THRESH) {
        lhash_resize(map);
      }
      // FIXME - this doesn't always work (but will in this hashtable
      //         implementation)
      if (lv_gettype(key) == LNUMBER) {
        double len = lv_getnumber(key);
        if ((u64)len == len && len > map->length) {
          map->length = (u32) len;
        }
      }
      break;
    }
  }
}

/**
 * @brief Internal helper to resize a table
 * @private
 */
static void lhash_resize(lhash_t *map) {
  u32 i, end = map->cap;
  map->cap *= 2;
  map->cap += 1;
  struct lh_pair *old = map->hash;
  map->hash = xmalloc(map->cap * sizeof(map->hash[0]));

  for (i = 0; i < map->cap; i++) {
    map->hash[i].key = LUAV_NIL;
  }

  for (i = 0; i < end; i++) {
    if (old[i].key != LUAV_NIL) {
      lhash_set(map, old[i].key, old[i].value);
    }
  }
  free(old);
}
