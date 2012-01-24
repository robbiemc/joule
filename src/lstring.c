#include <assert.h>
#include <string.h>
#include "lstring.h"
#include "util.h"

#define STRING_TABLE_CAP 128
#define STRING_HASHMAP_CAP 251
#define EMPTY ((void*)1)
#define NONEMPTY(p) (((u64)(p) & (u64)~0xF) == 0)

// universal string array
size_t str_table_cap = STRING_TABLE_CAP / 2;
size_t str_table_next = 0;
lstring_t *str_table = NULL;

// string hash map
typedef struct {
  lstring_t **table;
  size_t    capacity;
  size_t    size;
} smap_t;
smap_t smap = {NULL, (STRING_HASHMAP_CAP - 1) / 2, 0};

static void smap_insert(lstring_t *lstr);
static lstring_t *smap_lookup(char *str, size_t size);
static int smap_equal(lstring_t *lstr, char *str, size_t size);
static u32 smap_hash(u8 *str, size_t size);
static void smap_ins(smap_t *map, lstring_t *str);

size_t lstr_add(char *str, size_t size, int freeable) {
  // lookup the string in the hashset (see if it's already stored)
  lstring_t *lstr = smap_lookup(str, size);
  if (lstr == NULL) {
    // resize the table if necessary
    if (str_table_next == str_table_cap) {
      str_table_cap *= 2;
      str_table = xrealloc(str_table, str_table_cap);
    }
    // now add the string to the table
    lstr = &(str_table[str_table_next++]);
    lstr->length = size;
    lstr->ptr = str;
    lstr->hash = smap_hash((u8*) str, size);
    smap_insert(lstr);
  } else if (freeable) {
    free(str);
  }
  return (size_t) (lstr - str_table);
}

lstring_t *lstr_get(size_t index) {
  assert(index < smap.capacity);
  return smap.table[index];
}


static void smap_insert(lstring_t *lstr) {
  size_t i;
  // maybe resize the table
  if (smap.size*100 / smap.capacity > 70) {
    // create the new hassmap
    smap_t new_map;
    new_map.size = 0;
    new_map.capacity = smap.capacity * 2 + 1;
    new_map.table = xcalloc(new_map.capacity, sizeof(lstring_t*));
    // copy the old data into the new map
    for (i = 0; i < smap.capacity; i++) {
      lstring_t *s = smap.table[i];
      if (NONEMPTY(s))
        smap_ins(&new_map, s);
    }
    // replace the old map
    free(smap.table);
    memcpy(&smap, &new_map, sizeof(smap_t));
  }
  // insert the new string
  smap_ins(&smap, lstr);
}

static void smap_ins(smap_t *map, lstring_t *str) {
  size_t idx = str->hash % map->capacity;
  while (NONEMPTY(map->table[idx]))
    idx = (idx + 1) % map->capacity;
  map->table[idx] = str;
  map->size++;
}

static lstring_t *smap_lookup(char *str, size_t size) {
  lstring_t *s;
  u32 hash = smap_hash((u8*) str, size);
  size_t idx = hash % smap.capacity;
  while ((s = smap.table[idx]) != NULL) {
    if (smap_equal(s, str, size))
      return s;
    idx = (idx + 1) % smap.capacity;
  }
  return NULL;
}

static int smap_equal(lstring_t *lstr, char *str, size_t size) {
  if (lstr->length != size)
    return 0;
  return !memcmp(lstr->ptr, str, size);
}

// TODO - make this faster
static u32 smap_hash(u8 *str, size_t size) {
  u32 h = 17;
  while (size--) {
    h = h*29 ^ *str;
    str++;
  }
  return h;
}
