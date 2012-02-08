#include <assert.h>
#include <string.h>

#include "lstring.h"
#include "luav.h"
#include "panic.h"
#include "util.h"

#define LOAD_FACTOR 60
#define STRING_HASHMAP_CAP 251
#define NONEMPTY(p) ((u64)(p) > 1)

typedef struct {
  lstring_t **table;
  size_t    capacity;
  size_t    size;
} smap_t;
smap_t smap = {NULL, STRING_HASHMAP_CAP, 0};

int initialized = 0;
lstring_t empty;

static void smap_insert(lstring_t *str);
static void smap_ins(smap_t *map, lstring_t *str);
static lstring_t *smap_lookup(lstring_t *str);
static int smap_equal(lstring_t *str1, lstring_t *str2);
static u32 smap_hash(u8 *str, size_t size);

INIT static void lstr_init() {
  smap.table = xcalloc(smap.capacity, sizeof(smap.table[0]));
  initialized = 1;

  empty.length = 0;
  empty.data[0] = 0;
  lstr_add(&empty);
}

DESTROY static void lstr_destroy() {
  free(smap.table);
}

lstring_t *lstr_empty() {
  return &empty;
}

lstring_t *lstr_alloc(size_t size) {
  lstring_t *str = xmalloc(sizeof(lstring_t) + size);
  str->length = size;
  str->data[0] = 0;
  return str;
}

lstring_t *lstr_add(lstring_t *str) {
  xassert(initialized);
  assert(str->data[str->length] == 0);
  // compute the hash of the string
  str->hash = smap_hash((u8*) str->data, str->length);
  // lookup the string in the hashset (see if it's already stored)
  lstring_t *found = smap_lookup(str);
  if (NONEMPTY(found)) {
    free(str);
    return found;
  }
  // now add the new string to the table
  smap_insert(str);
  return str;
}

lstring_t *lstr_literal(char *cstr) {
  size_t size = strlen(cstr);
  lstring_t *str = lstr_alloc(size);
  str->length = size;
  memcpy(str->data, cstr, size+1);
  return lstr_add(str);
}

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
  while (NONEMPTY(map->table[idx])) {
    idx = (idx + step) % cap;
    step++;
  }
  map->table[idx] = str;
  map->size++;
}

static lstring_t *smap_lookup(lstring_t *str) {
  lstring_t *s;
  size_t cap = smap.capacity;
  size_t idx = str->hash % cap;
  size_t step = 1;
  while (NONEMPTY(s = smap.table[idx])) {
    if (smap_equal(str, s))
      return s;
    idx = (idx + step) % cap;
    step++;
  }
  return NULL;
}

static int smap_equal(lstring_t *str1, lstring_t *str2) {
  if (str1->length != str2->length)
    return 0;
  return !memcmp(str1->data, str2->data, str1->length);
}

static u32 smap_hash(u8 *str, size_t size) {
  // figure out a step value
  size_t step = (size >> 5) + 1;
  // compute the hash
  u32 hash = lv_hash(size);
  u8 *end = str + size;
  while (str < end) {
    hash = hash ^ ((hash << 5) + (hash >> 2) + *str);
    str += step;
  }
  return hash;
}
