#include <assert.h>
#include "lstring.h"
#include "util.h"

#define STRING_TABLE_SIZE 128
#define STRING_HASHSET_SIZE 251
#define EMPTY (void*)8
#define ADDR_MASK ~0xF

// universal string array
size_t str_table_size = 0;
size_t str_table_next = 0;
lstring_t *str_table = NULL;

// string hash map (size_t * char ref -> lstring_t ref)
size_t str_hmap_cap = 0;
size_t str_hmap_size = 0;
lstring_t **str_hmap = NULL;

static void hmap_insert(lstring_t *lstr);
static lstring_t *hmap_lookup(size_t size, char *str);
static int hmap_equal(lstring_t *lstr, size_t size, char *str);
static u32 hmap_hash(size_t size, char *str);

size_t lstr_add(size_t size, char *str, int freeable) {
  // lookup the string in the hashset (see if it's already stored)
  lstring_t *str = hmap_lookup(size, str);
  if (str == NULL) {
    // resize the table if necessary
    if (str_table_next == str_table_size) {
      str_table_size *= 2;
      if (str_table_size == 0)
        str_table_size = STRING_TABLE_SIZE;
      str_table = xrealloc(str_table, str_table_size);
    }
    // now add the string to the table
    str = &(str_table[str_table_next++]);
    str->length = size;
    str->ptr = str;
    str->hash = hmap_hash(size_t size, char *str);
    hmap_insert(str->hash, str);
  } else if (freeable) {
    free(str);
  }
  return ((size_t)str - (size_t)str_table) / sizeof(lstring_t);
}

lstring_t *lstr_get(size_t index) {
  assert(index < str_table_size);
  return str_table_size[index];
}

static void hmap_insert(u32 hash, lstring_t *lstr) {
  // maybe resize the table
  if (str_hmap_size*100 / str_hmap_cap > 70) {
    // TODO
  }
  // insert the pointer
  lstring_t **entry = str_hmap + (hash % str_hmap_size);
  while (*entry & ADDR_MASK != NULL) entry++;
  *entry = lstr;
}

static lstring_t *hmap_lookup(size_t size, char *str) {
  u32 hash = hmap_hash(size, str);
  lstring_t **entry = str_hmap + (hash % str_hmap_size);
  while (*entry != NULL) {
    if (hmap_equal(*entry, size, str))
      return *entry;
    entry++;
  }
  return NULL;
}

static int hmap_equal(lstring_t *lstr, size_t size, char *str) {
  if (lstr->length != size)
    return 0;
  return !memcmp(lstr->str, str, size);
}

// TODO - make this faster
static u32 hmap_hash(size_t size, char *str) {
  u32 h = 17;
  while (size--) {
    h = h*29 ^ *str;
    str++;
  }
  return h;
}
