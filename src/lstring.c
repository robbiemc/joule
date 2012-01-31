#include <assert.h>
#include <string.h>

#include "lstring.h"
#include "util.h"

#define LOAD_FACTOR 50
#define MAX_HASH 256
#define STRING_TABLE_CAP 128
#define STRING_HASHMAP_CAP 251
#define NONEMPTY(p) ((p) > 1)

// universal string array
static size_t str_table_cap = STRING_TABLE_CAP;
static size_t str_table_next = 2;
static lstring_t *str_table = NULL;
static char zerostr = '\0';
static int initialized = 0;

// string hash map
typedef struct {
  lstr_idx  *table;
  size_t    capacity;
  size_t    size;
} smap_t;
smap_t smap = {NULL, STRING_HASHMAP_CAP, 0};

static void smap_insert(lstring_t *lstr, lstr_idx index);
static void smap_ins(smap_t *map, lstring_t *str, lstr_idx index);
static size_t smap_lookup(char *str, size_t size);
static int smap_equal(lstring_t *lstr, char *str, size_t size);
static u32 smap_hash(u8 *str, size_t size);

INIT static void lstr_init() {
  str_table = xcalloc(str_table_cap, sizeof(str_table[0]));
  smap.table = xcalloc(smap.capacity, sizeof(smap.table[0]));
  initialized = 1;
}

DESTROY static void lstr_destroy() {
  free(str_table);
  free(smap.table);
}

lstr_idx lstr_add(char *str, size_t size, int freeable) {
  assert(initialized);
  // lookup the string in the hashset (see if it's already stored)
  if (size == 0) {
    if (freeable) free(str);
    str = &zerostr;
  }
  assert(str[size] == 0);
  lstr_idx index = smap_lookup(str, size);
  if (NONEMPTY(index)) {
    if (freeable)
      free(str);
    return index;
  }

  // resize the table if necessary
  if (str_table_next == str_table_cap) {
    str_table_cap *= 2;
    str_table = xrealloc(str_table, str_table_cap * sizeof(str_table[0]));
  }

  // now add the string to the table
  lstring_t *lstr = &(str_table[str_table_next]);
  lstr->length = size;
  lstr->ptr = str;
  lstr->hash = smap_hash((u8*) str, size);
  smap_insert(lstr, str_table_next);
  return str_table_next++;
}

lstring_t *lstr_get(lstr_idx index) {
  if (index >= str_table_cap)
    return NULL;
  return &str_table[index];
}

static void smap_insert(lstring_t *lstr, lstr_idx index) {
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
      size_t ix = smap.table[i];
      if (NONEMPTY(ix))
        smap_ins(&new_map, &str_table[ix], ix);
    }
    // replace the old map
    free(smap.table);
    smap = new_map;
  }
  // insert the new string
  smap_ins(&smap, lstr, index);
}

static void smap_ins(smap_t *map, lstring_t *str, lstr_idx index) {
  size_t idx = str->hash % map->capacity;
  while (NONEMPTY(map->table[idx]))
    idx = (idx + 1) % map->capacity;
  map->table[idx] = index;
  map->size++;
}

static size_t smap_lookup(char *str, size_t size) {
  size_t s;
  u32 hash = smap_hash((u8*) str, size);
  size_t idx = hash % smap.capacity;
  while (NONEMPTY(s = smap.table[idx])) {
    if (smap_equal(&str_table[s], str, size))
      return s;
    idx = (idx + 1) % smap.capacity;
  }
  return 0;
}

static int smap_equal(lstring_t *lstr, char *str, size_t size) {
  if (lstr->length != size)
    return 0;
  return !memcmp(lstr->ptr, str, size);
}

static u32 smap_hash(u8 *str, size_t size) {
  // figure out a step value
  size_t step = size / MAX_HASH;
  step = (step == 0) ? 1 : step;
  // compute the hash
  u32 hash = 0;
  u8 *end = str + size;
  while (str < end) {
    hash = *str + (hash << 6) + (hash << 16) - hash;
    str += step;
  }
  return hash;
}
