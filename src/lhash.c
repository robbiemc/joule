/**
 * @brief Hash table implementation
 *
 * Currently very simple, lots of room for optimization.
 *
 * TODO: iterate over metamethods in lhash_next
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "gc.h"
#include "lhash.h"
#include "luav.h"
#include "meta.h"
#include "util.h"
#include "vm.h"

#define LHASH_ARRAY 0
#define LHASH_TABLE 1
#define DOWNSIZE    -1
#define UPSIZE      1

static void lhash_resize(lhash_t *hash, int which, int direction);
static int  lhash_index(lhash_t *map, luav key, i32 *index);

luav meta_strings[NUM_META_METHODS];
static luav str__G;
static lhash_t *init_hash = NULL;

/**
 * @brief Preserves whatever hash is in the middle of initialization for
 *        garbage collection
 *
 * If a hash is being initialized, it's probably not located anywhere in the
 * global tables of lua values, so we need to preserve it in case allocation of
 * sub components triggers GC. This preserves whatever hash is being initialized
 * if GC is triggered in lhash_init()
 */
static void lhash_preserve_init_hash() {
  gc_traverse_pointer(init_hash, LTABLE);
}

INIT static void lua_lhash_init() {
  meta_strings[META_ADD_IDX]       = LSTR("__add");
  meta_strings[META_SUB_IDX]       = LSTR("__sub");
  meta_strings[META_MUL_IDX]       = LSTR("__mul");
  meta_strings[META_DIV_IDX]       = LSTR("__div");
  meta_strings[META_MOD_IDX]       = LSTR("__mod");
  meta_strings[META_POW_IDX]       = LSTR("__pow");
  meta_strings[META_UNM_IDX]       = LSTR("__unm");
  meta_strings[META_CONCAT_IDX]    = LSTR("__concat");
  meta_strings[META_LEN_IDX]       = LSTR("__len");
  meta_strings[META_EQ_IDX]        = LSTR("__eq");
  meta_strings[META_LT_IDX]        = LSTR("__lt");
  meta_strings[META_LE_IDX]        = LSTR("__le");
  meta_strings[META_INDEX_IDX]     = LSTR("__index");
  meta_strings[META_NEWINDEX_IDX]  = LSTR("__newindex");
  meta_strings[META_CALL_IDX]      = LSTR("__call");
  meta_strings[META_METATABLE_IDX] = LSTR("__metatable");
  meta_strings[META_TOSTRING_IDX]  = LSTR("__tostring");
  str__G = LSTR("_G");

  gc_add_hook(lhash_preserve_init_hash);
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
  init_hash = map;
  memset(map, 0, sizeof(lhash_t));
  map->tcap = LHASH_INIT_TSIZE;
  map->acap = LHASH_INIT_ASIZE;

  map->table = gc_alloc(LHASH_INIT_TSIZE * sizeof(map->table[0]), LANY);
  for (i = 0; i < LHASH_INIT_TSIZE; i++) {
    map->table[i].key = LUAV_NIL;
  }
  map->array = gc_alloc(LHASH_INIT_ASIZE * sizeof(map->array[0]), LANY);
  lv_nilify(map->array, LHASH_INIT_ASIZE);
  init_hash = NULL;
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
  i32 index;
  assert(!lv_isupvalue(key));
  if (key == LUAV_NIL) {
    return LUAV_NIL;
  } else if (map == lua_globals && key == str__G) {
    return lv_table(lua_globals);
  }

  if (lv_isnumber(key)) {
    double n = lv_cvt(key);
    index = (i32) n;
    if (isnan(n)) {
      return LUAV_NIL;
    } else if ((double) index == n && index > 0 && (u32) index < map->acap) {
      return map->array[index];
    }
  }

  if (!lhash_index(map, key, &index)) {
    return LUAV_NIL;
  }
  return map->table[index].value;
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
  i32 index;
  assert(!lv_isupvalue(key));
  assert(!lv_isupvalue(value));

  if (key == LUAV_NIL || (lv_isnumber(key) && isnan(lv_cvt(key)))) {
    err_rawstr("table index is nil", TRUE);
  }

  if (lv_isnumber(key)) {
    double n = lv_cvt(key);
    index = (i32) n;
    if ((double) index == n && index > 0) {
      /* Try to put the value in the already-allocated array */
      if ((u32) index < map->acap) {
        luav prev = map->array[index];
        map->array[index] = value;
        /* Added a new element, increase the length */
        if (prev == LUAV_NIL && value != LUAV_NIL) {
          map->asize++;
          if ((u32) index > map->length) {
            map->length = (u32) index;
            assert(map->length <= map->acap);
          }
        /* Added a new element, upsize */
        } else if (prev != LUAV_NIL && value == LUAV_NIL) {
          map->asize--;
          /* TODO: this is O(n), bad? */
          if ((u32) index == map->length) {
            i32 i;
            for (i = index - 1; i >= 0; i--) {
              if (map->array[i] != LUAV_NIL) { break; }
            }
            if (i == -1) {
              map->length = 0;
            } else {
              map->length = (u32) i;
            }
          }
        }
        return;

      /* See if we should resize the array portion */
      } else if (value != LUAV_NIL && (u32) index < map->acap * 2 &&
                 map->asize >= map->acap / 2) {
        lhash_resize(map, LHASH_ARRAY, UPSIZE);
        assert(map->array[index] == LUAV_NIL);
        map->array[index] = value;
        map->length = (u32) index;
        return;
      }
    }
  }

  if (lhash_index(map, key, &index)) {
    map->table[index].value = value;
    if (value == LUAV_NIL) {
      map->tsize--;
    }
  } else if (value == LUAV_NIL) {
    return;
  } else {
    assert(index >= 0);
    map->table[index].key = key;
    map->table[index].value = value;
    map->tsize++;
  }
  if (map->tsize * 100 / map->tcap > LHASH_MAP_THRESH) {
    lhash_resize(map, LHASH_TABLE, UPSIZE);
  } else if (value == LUAV_NIL &&
             map->tsize * 100 / (map->tcap / 2) < LHASH_MAP_THRESH &&
             map->tsize > LHASH_INIT_TSIZE) {
    lhash_resize(map, LHASH_TABLE, DOWNSIZE);
  }
}

/**
 * @brief Internal helper to resize a hash
 *
 * Resizes both the array and table portions of the hash, because elements
 * could possibly shift between the two.
 *
 * @param map the hash to resize
 * @param which which portion of the hash to resize
 * @param direction direction in which to resize (up or down)
 * @private
 */
static void lhash_resize(lhash_t *map, int which, int direction) {
  u32 i, tend = map->tcap;
  struct lh_pair *old = map->table;
  if (map->flags & LHASH_RESIZING ||
      (map->flags & LHASH_ITERATING && direction == DOWNSIZE)) {
    return;
  }
  map->flags |= LHASH_RESIZING;

  /* If we're resizing the table portion, there is no reason that we should
     touch the array portion of the map */
  if (which == LHASH_TABLE) {
    /* Can't set map->table before gc_alloc because of garbage collection */
    u32 newsize;
    if (direction == UPSIZE) {
      newsize = 2 * map->tcap + 1;
    } else {
      newsize = map->tcap / 2 + 1;
    }
    map->table = gc_alloc(newsize * sizeof(map->table[0]), LANY);
    map->tcap  = newsize;
    map->tsize = 0;
    /* Make sure all new keys are nil */
    for (i = 0; i < map->tcap; i++) {
      map->table[i].key = LUAV_NIL;
    }
    for (i = 0; i < tend; i++) {
      if (old[i].key != LUAV_NIL) {
        lhash_set(map, old[i].key, old[i].value);
      }
    }
    map->flags &= ~LHASH_RESIZING;
    return;
  }

  /* TODO: downsize array portion of the table */
  assert(direction == UPSIZE);
  assert(which == LHASH_ARRAY);
  map->acap *= 2;
  map->array = gc_realloc(map->array, map->acap * sizeof(map->array[0]));

  lv_nilify(map->array + map->acap / 2, map->acap / 2);

  /* Move any integer keys into the array section if we can */
  for (i = 0; i < tend; i++) {
    luav key = old[i].key;
    if (lv_isnumber(key)) {
      double n = lv_cvt(key);
      i32 index = (i32) n;
      if ((double) index == n && index > 0 && (u32) index < map->acap) {
        map->array[index] = old[i].value;
        old[i].value = LUAV_NIL;
        if ((u32) index > map->length) {
          map->length = (u32) index;
        }
        map->asize++;
        map->tsize--;
      }
    }
  }
  map->flags &= ~LHASH_RESIZING;
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
static int lhash_index(lhash_t *map, luav key, i32 *index) {
  i32 cap  = (i32) map->tcap;
  i32 h    = (i32) (lv_hash(key) % map->tcap);
  i32 step = 0;
  i32 hole = -1;

  while (step < cap) {
    h += step++;
    if (h >= cap) {
      h -= cap;
      assert(h < cap);
    }
    assert(h >= 0);
    struct lh_pair *entry = &map->table[h];
    luav cur = entry->key;
    if (cur == key) {
      *index = h;
      return (entry->value != LUAV_NIL);
    } else if (cur == LUAV_NIL) {
      *index = hole == -1 ? h : hole;
      return FALSE;
    } else if (hole == -1 && entry->value == LUAV_NIL) {
      hole = h;
    }
  }

  assert(hole >= 0);
  *index = hole;
  return FALSE;
}

/**
 * @brief Implementation of the lua next() function
 *
 * Given a key, iterates to the next key, returning the next key/value pair.
 * If the end of the table has been reached, then the returned pair are both
 * nil.
 *
 * @param map the map to iterate
 * @param key the previous return value of next(), placeholder key
 * @param nxtkey pointer to fill in for the next key value
 * @param nxtval pointer to fill in for the next value
 */
void lhash_next(lhash_t *map, luav key, luav *nxtkey, luav *nxtval) {
  struct lh_pair *entry;
  u32 i, h = 0;
  map->flags |= LHASH_ITERATING;

  /* We must begin by iterating over the array portion of the table */
  if (lv_isnumber(key) || key == LUAV_NIL) {
    double n;
    i32 index;
    /* First time? start at one, otherwise, convert to a number */
    if (key == LUAV_NIL) {
      n = 1;
      index = 1;
    } else {
      n = lv_cvt(key);
      index = (i32) n;
    }
    /* Only check if our index is an integer within range */
    if ((double) index == n && index > 0 && (u32) index < map->acap) {
      /* Skip over what we were just looking at */
      if (key != LUAV_NIL) { index++; }
      for (i = (u32) index; i <= map->length; i++) {
        assert(i < map->acap);
        if (map->array[i] != LUAV_NIL) {
          *nxtkey = lv_number(i);
          *nxtval = map->array[i];
          return;
        }
      }
      /* Start iterating through the table portion */
      key = LUAV_NIL;
    }
  }

  if (key != LUAV_NIL) {
    /* Find where our key is (taking collisions into account), then increment
       the index to move on to the next cell */
    h = lv_hash(key) % map->tcap;
    while (map->table[h].key != key) {
      h = (h + 1) % map->tcap;
    }
    h++;
  }

  for (i = h; i < map->tcap; i++) {
    entry = &map->table[i];
    if (entry->key != LUAV_NIL && entry->value != LUAV_NIL) {
      *nxtkey = map->table[i].key;
      *nxtval = map->table[i].value;
      return;
    }
  }

  *nxtkey = LUAV_NIL;
  *nxtval = LUAV_NIL;
  map->flags &= ~LHASH_ITERATING;
}

/**
 * @brief Finds the maximum positive numerical index in the given map
 *
 * Meant to be used by the table.maxn() lua method. Does a linear traversal of
 * the entire map to find the maximum indice, so it's not recommended that this
 * is called often.
 *
 * @param map the table to look into
 * @return the maximum index found, or 0 if no index is found
 */
double lhash_maxn(lhash_t *map) {
  double maxi = (double) map->length; /* maximum in array portion */
  u32 i;
  for (i = 0; i < map->tcap; i++) {
    if (lv_isnumber(map->table[i].key) && map->table[i].value != LUAV_NIL) {
      double n = lv_cvt(map->table[i].key);
      maxi = MAX(maxi, n);
    }
  }
  return maxi;
}

/**
 * @brief Inserts the given value at the given position into the table.
 *
 * The table is intepreted as an array, so values with keys as consecutive
 * integers at and after the given position are shifted up by one.
 *
 * @param map the table to insert into
 * @param pos the position to insert at
 * @param value the value to insert at the specified position.
 */
void lhash_insert(lhash_t *map, u32 pos, luav value) {
  if (pos > map->acap) {
    lhash_set(map, lv_number(pos), value);
    return;
  }
  if (map->length + 1 >= map->acap) {
    lhash_resize(map, LHASH_ARRAY, UPSIZE);
  }
  assert(map->length + 1 < map->acap);

  /* Make some room */
  memcpy(&map->array[pos + 1], &map->array[pos],
         (map->length - pos + 1) * sizeof(luav));
  map->array[pos] = value;
  map->length++;
}

/**
 * @brief Removes an element at the specified position in a table
 *
 * The table is intepreted as an array, so values with keys as consecutive
 * integers after the given position are shifted down by one.
 *
 * @param map the table to remove from
 * @param pos the position to remove
 */
luav lhash_remove(lhash_t *map, u32 pos) {
  assert(map->length < map->acap);
  if (pos > map->length) {
    return LUAV_NIL;
  }
  luav ret = map->array[pos];
  memmove(&map->array[pos], &map->array[pos + 1],
          (map->length - pos) * sizeof(luav));
  /* Recalculate the length now */
  map->length--;
  while (map->length > 0 && map->array[map->length] == LUAV_NIL) {
    map->length--;
  }
  return ret;
}

/**
 * @brief Sort the array portion of the table
 *
 * With the current implementation, just sorts the array portion of the table
 * which have integer keys.
 *
 * @param map the hash to sort
 * @param comparator the comparator to use.
 */
void lhash_sort(lhash_t *map, lcomparator_t *comparator) {
  qsort(map->array + 1, map->length, sizeof(luav),
        (int(*)(const void*, const void*)) comparator);
}
