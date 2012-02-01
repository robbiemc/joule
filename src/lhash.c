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
#include <string.h>

#include "lhash.h"
#include "luav.h"
#include "meta.h"
#include "util.h"

static void lhash_resize(lhash_t *hash);

static luav meta_strings[NUM_META_METHODS];
static luav max_meta_string;


INIT static void lua_lhash_init() {
  meta_strings[META_ADD]       = LSTR("__add");
  meta_strings[META_SUB]       = LSTR("__sub");
  meta_strings[META_MUL]       = LSTR("__mul");
  meta_strings[META_DIV]       = LSTR("__div");
  meta_strings[META_MOD]       = LSTR("__mod");
  meta_strings[META_POW]       = LSTR("__pow");
  meta_strings[META_UNM]       = LSTR("__unm");
  meta_strings[META_CONCAT]    = LSTR("__concat");
  meta_strings[META_LEN]       = LSTR("__len");
  meta_strings[META_EQ]        = LSTR("__eq");
  meta_strings[META_LT]        = LSTR("__lt");
  meta_strings[META_LE]        = LSTR("__le");
  meta_strings[META_INDEX]     = LSTR("__index");
  meta_strings[META_NEWINDEX]  = LSTR("__newindex");
  meta_strings[META_CALL]      = LSTR("__call");
  meta_strings[META_METATABLE] = LSTR("__metatable");

  // an ugly hack that speeds up lhash_check_meta *A LOT*
  size_t i;
  max_meta_string = 0;
  for (i = 0; i < NUM_META_METHODS; i++) {
    if (meta_strings[i] > max_meta_string)
      max_meta_string = meta_strings[i];
  }
}

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
  map->metatable = NULL;
  map->metamethods = NULL;
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
  if (hash->metamethods != NULL) {
    free(hash->metamethods);
  }
  free(hash->hash);
}

/**
 * @brief Checks whether the given key is a metatable event and if so, returns
 *        its index
 *
 * @param key the key to check
 * @return the metatable index of the given key, or META_INVALID if it is not a
 *         valid event
 */
size_t lhash_check_meta(luav key) {
  if (!lv_istype(key, LSTRING) || key > max_meta_string)
    return META_INVALID;

  size_t i;
  for (i = 0; i < NUM_META_METHODS; i++) {
    if (meta_strings[i] == key) {
      return i;
    }
  }
  return META_INVALID;
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
  assert(!lv_istype(key, LUPVALUE));
  if (key == LUAV_NIL) {
    return LUAV_NIL;
  }

  // check if it's a metatable key
  size_t meta_index = lhash_check_meta(key);
  if (meta_index != META_INVALID) {
    if (map->metamethods == NULL) {
      return LUAV_NIL;
    }
    return map->metamethods[meta_index];
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
  assert(!lv_istype(key, LUPVALUE));
  assert(!lv_istype(value, LUPVALUE));

  // check if it's a metatable key
  size_t meta_index = lhash_check_meta(key);
  if (meta_index != META_INVALID) {
    if (map->metamethods == NULL) {
      map->metamethods = xmalloc(NUM_META_METHODS * sizeof(luav));
      size_t i;
      for (i = 0; i < NUM_META_METHODS; i++)
        map->metamethods[i] = LUAV_NIL;
    }
    map->metamethods[meta_index] = value;
  }

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
      if (lv_istype(key, LNUMBER)) {
        double len = lv_castnumber(key, 0);
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
