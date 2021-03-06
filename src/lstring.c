#include <assert.h>
#include <string.h>

#include "gc.h"
#include "lstring.h"
#include "luav.h"
#include "panic.h"
#include "util.h"

#define LOAD_FACTOR 60
#define STRING_HASHMAP_CAP 251
#define NONEMPTY(p) ((size_t)(p) > 1)
#define LSTR_EMPTY ((lstring_t*) 1)

typedef struct {
  lstring_t **table;
  size_t    capacity;
  size_t    size;
} smap_t;
smap_t smap = {NULL, STRING_HASHMAP_CAP, 0};

static int initialized = 0; //<! Sanity check
static lstring_t *empty;    //<! Unique empty string

static void smap_insert(lstring_t *str);
static void smap_ins(smap_t *map, lstring_t *str);
static ssize_t smap_lookup(lstring_t *str);
static u32 smap_hash(u8 *str, size_t size);
static void lstring_gc();

EARLY(0) static void lstr_init() {
  smap.table = xcalloc(smap.capacity, sizeof(smap.table[0]));
  initialized = 1;
  empty = lstr_literal("", 1);
  gc_add_hook(lstring_gc);
}

DESTROY static void lstr_destroy() {
  free(smap.table);
  smap.table = NULL;
}

static void lstring_gc() {
  size_t i;
  for (i = 0; i < smap.capacity; i++) {
    if (NONEMPTY(smap.table[i]) && smap.table[i]->permanent) {
      gc_traverse_pointer(smap.table[i], LSTRING);
    }
  }
}

/**
 * @brief Quick method for fetching the canonical empty string
 */
lstring_t *lstr_empty() {
  return empty;
}

/**
 * @brief Allocates a string of a specified size
 *
 * @param size the size to allocate
 * @return the allocated string
 */
lstring_t *lstr_alloc(size_t size) {
  lstring_t *str = gc_alloc(sizeof(lstring_t) + size, LSTRING);
  str->length  = size;
  str->data[0] = 0;
  str->hash    = 0;
  str->permanent = 0;
  return str;
}

/**
 * @brief Reallocate the space for a given string
 *
 * @param str the string to reallocate space for
 * @param size the new size to assume, or if 0, the size is doubled
 * @return the reallocated string
 */
lstring_t *lstr_realloc(lstring_t *str, size_t size) {
  assert(str->hash == 0);
  if (size == 0) size = str->length * 2;
  str->length = size;
  return gc_realloc(str, sizeof(lstring_t) + size);
}

/**
 * @brief Add a new string to the global table
 *
 * If the string is already located in the global table, then the provided
 * string is ignored and the global string is returned.
 *
 * @param str the gc-allocated string
 * @return the canonical representation of the provided string
 */
lstring_t *lstr_add(lstring_t *str) {
  xassert(initialized);
  assert(str->hash == 0);
  assert(str->data[str->length] == 0);
  // compute the hash of the string
  str->hash = smap_hash((u8*) str->data, str->length);
  // lookup the string in the hashset (see if it's already stored)
  ssize_t found = smap_lookup(str);
  if (found >= 0) {
    return smap.table[found];
  }
  // now add the new string to the table
  smap_insert(str);
  return str;
}

/**
 * @brief Creates a new lua string based off the literal C-string
 *
 * @param cstr the null-terminated C-string
 * @return the canonical lstring_t to represent the provided string
 */
lstring_t *lstr_literal(char *cstr, int retain) {
  size_t size = strlen(cstr);
  lstring_t *str = lstr_alloc(size);
  str->length = size;
  str->permanent = (u32) retain;
  memcpy(str->data, cstr, size + 1);
  lstring_t *actual = lstr_add(str);
  return actual;
}

/**
 * @brief Performs a lexical comparison on two lstrings
 *
 * @param s1 the first string
 * @param s2 the second string
 * @return what memcmp returns
 */
int lstr_compare(lstring_t *s1, lstring_t *s2) {
  size_t minlen = MIN(s1->length, s2->length);
  int cmp = memcmp(s1->data, s2->data, minlen);
  if (cmp != 0) return cmp;
  if (s1->length < s2->length) return -1;
  return s1->length > s2->length;
}

// lstring hash map stuff =====================================================

static void smap_insert(lstring_t *str) {
  size_t i;
  // maybe resize the table
  if (smap.size*100 / smap.capacity > LOAD_FACTOR) {
    // create the new hashmap
    smap_t new_map;
    new_map.size = 0;
    new_map.capacity = smap.capacity * 2 + 1;
    new_map.table = xcalloc(new_map.capacity, sizeof(new_map.table[0]));
    // copy the old data into the new map
    for (i = 0; i < smap.capacity; i++) {
      lstring_t *val = smap.table[i];
      if (NONEMPTY(val))
        smap_ins(&new_map, val);
    }
    // replace the old map
    free(smap.table);
    smap = new_map;
  }
  // insert the new string
  smap_ins(&smap, str);
}

static void smap_ins(smap_t *map, lstring_t *str) {
  size_t cap = map->capacity;
  size_t idx = str->hash % cap;
  size_t step = 1;
  while (map->table[idx] != NULL) {
    idx = (idx + step) % cap;
    step++;
  }
  map->table[idx] = str;
  map->size++;
}

static ssize_t smap_lookup(lstring_t *str) {
  lstring_t *s;
  size_t cap = smap.capacity;
  size_t idx = str->hash % cap;
  size_t step = 1;
  while ((s = smap.table[idx]) != NULL) {
    if (s != LSTR_EMPTY &&
        str->length == s->length &&
        memcmp(str->data, s->data, str->length) == 0)
      return (ssize_t) idx;
    idx = (idx + step) % cap;
    step++;
  }
  return -1;
}

static u32 smap_hash(u8 *str, size_t size) {
  // figure out a step value
  size_t step = (size >> 5) + 1;
  // compute the hash
  u32 hash = lv_hash((luav) size);
  u8 *end = str + size;
  while (str < end) {
    hash = hash ^ ((hash << 5) + (hash >> 2) + *str);
    str += step;
  }
  return hash;
}

/**
 * @brief Remove a string from the global hash table
 *
 * This should only be called when there are no references left to the provided
 * string, such as during garbage collection. The string's data is not freed.
 *
 * @param str the string to remove.
 */
void lstr_remove(lstring_t *str) {
  if (smap.table == NULL) {
    return;
  }
  ssize_t idx = smap_lookup(str);
  if (idx >= 0 && smap.table[idx] == str) {
    smap.table[idx] = LSTR_EMPTY;
  }
}

/**
 * @brief Concatenate two strings
 *
 * @param s1 the first string
 * @param s2 the second string
 * @return the two strings concatenated
 */
lstring_t* lstr_concat(lstring_t *s1, lstring_t *s2) {
  if (s1 == empty) return s2;
  if (s2 == empty) return s1;
  lstring_t *sn = lstr_alloc(s1->length + s2->length);
  memcpy(sn->data, s1->data, s1->length);
  memcpy(sn->data + s1->length, s2->data, s2->length);
  sn->data[s1->length + s2->length] = 0;
  return lstr_add(sn);
}
