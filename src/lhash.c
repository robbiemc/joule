/**
 * @brief Hash table implementation
 *
 * Currently very simple, lots of room for optimization.
 *
 * TODO: optimize
 * TODO: shrink when keys set to nil
 * TODO: iterate over metamethods in lhash_next
 * TODO: fix the len operator
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "lhash.h"
#include "luav.h"
#include "meta.h"
#include "util.h"

static void lhash_resize(lhash_t *hash);

luav meta_strings[NUM_META_METHODS];
static luav meta_empty[NUM_META_METHODS];
static luav max_meta_string;

INIT static void lua_lhash_init() {
  // a nil meta table array - this is default
  size_t i;
  for (i = 0; i < NUM_META_METHODS; i++)
    meta_empty[i] = LUAV_NIL;

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
  meta_strings[META_TOSTRING]  = LSTR("__tostring");

  // an ugly hack that speeds up lhash_check_meta *A LOT*
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
  map->metamethods = meta_empty;
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
  if (hash->metamethods != meta_empty) {
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
i32 lhash_check_meta(luav key) {
  if (!lv_isstring(key) || key > max_meta_string)
    return META_INVALID;

  i32 i;
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
  assert(!lv_isupvalue(key));
  if (key == LUAV_NIL) {
    return LUAV_NIL;
  }

  i32 index;
  int found = lhash_index(map, key, &index);
  if (!found) return LUAV_NIL;
  return lhash_rawget(map, index);
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
  assert(!lv_isupvalue(key));
  assert(!lv_isupvalue(value));

  if (key == LUAV_NIL || (lv_isnumber(key) && isnan(lv_cvt(key)))) {
    err_rawstr("table index is nil", TRUE);
  }

  i32 index;
  int found = lhash_index(map, key, &index);
  lhash_rawset(map, index, !found, key, value);
}

/**
 * @brief Finds which index the given key is at, or would be inserted at
 *
 * If the key is already in the table, 'index' is set to the index of the key,
 * otherwise, 'index' is set to the index the key would be inserted at. The
 * function returns TRUE if the key was found, and FALSE if it was not.
 *
 * @param map the table to look in
 * @param key the key to look up in the table
 * @param index where to store the index
 * @return TRUE if the key was found in the table
 */
int lhash_index(lhash_t *map, luav key, i32 *index) {
  // check if it's a metatable key
  i32 meta_index = lhash_check_meta(key);
  if (meta_index != META_INVALID) {
    *index = -meta_index;
    return TRUE;
  }

  i32 h = (i32) (lv_hash(key) % map->cap);
  i32 step = 1;
  while (1) {
    struct lh_pair *entry = &map->hash[h];
    luav cur = entry->key;
    if (cur == key) {
      *index = h;
      return (entry->value != LUAV_NIL);
    } else if (cur == LUAV_NIL) {
      *index = h;
      return FALSE;
    }
    h = (h + step++) % (i32) map->cap;
  }
}

/**
 * @brief Gets the value at the given index of the given table
 *
 * The index is not checked - it is assumed it is a valid index.
 *
 * @param map the table to look in
 * @param index the index into the table to return
 * @return the value at the given index - LUAV_NIL if there isn't one
 */
luav lhash_rawget(lhash_t *map, i32 index) {
  if (index < 0) {
    // metatable get
    return map->metamethods[-index];
  }
  return map->hash[index].value;
}

/**
 * @brief Sets the key/value at the given index of the given table
 *
 * The index is not checked - it is assumed it is a valid index.
 * TODO - if val is nil, we should decrease the size of the hashtable
 *
 * @param map the table to alter
 * @param index the index within the table to set
 * @param isnew TRUE if the key was previously nil
 * @param key the key to assign to map[index]
 * @param value the value to assign to map[index]
 */
void lhash_rawset(lhash_t *map, i32 index, int isnew, luav key, luav val) {
  if (index < 0) {
    // metatable set
    if (map->metamethods == meta_empty) {
      map->metamethods = xmalloc(NUM_META_METHODS * sizeof(luav));
      lv_nilify(map->metamethods, NUM_META_METHODS);
    }
    map->metamethods[-index] = val;
    return;
  }

  map->hash[index].key = key;
  map->hash[index].value = val;

  // update the array size
  if (isnew) {
    map->size++;
    if (map->size * 100 / map->cap > LHASH_MAP_THRESH) {
      lhash_resize(map);
    }
    // FIXME - this doesn't always work (but will in this hashtable
    //         implementation)
    if (lv_isnumber(key) && val != LUAV_NIL) {
      double len = lv_castnumber(key, 0);
      if ((u64)len == len && len > map->length) {
        map->length = (u32) len;
      }
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

void lhash_next(lhash_t *map, luav key, luav *nxtkey, luav *nxtval) {
  struct lh_pair *entry;
  u32 i, h = 0;
  if (key != LUAV_NIL) {
    h = lv_hash(key) % map->cap;
    do {
      entry = &map->hash[h];
      h++;
    } while (h < map->cap && entry->key != key);
  }
  for (i = h; i < map->cap; i++) {
    entry = &map->hash[i];
    if (entry->key != LUAV_NIL && entry->value != LUAV_NIL) {
      *nxtkey = map->hash[i].key;
      *nxtval = map->hash[i].value;
      return;
    }
  }
  *nxtkey = LUAV_NIL;
  *nxtval = LUAV_NIL;
}
