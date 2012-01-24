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

#include "lhash.h"
#include "luav.h"

typedef struct lpair {
  luav key;
  luav value;
} lpair_t;

struct lhash {
  u32 cap;
  u32 size;
  lpair_t *hash;
};

static void lhash_resize(lhash_t *hash);

lhash_t* lhash_alloc() {
  int i;
  lhash_t *hash = xmalloc(sizeof(lhash_t));
  hash->cap = LHASH_INIT_SIZE;
  hash->size = 0;
  hash->hash = xmalloc(LHASH_INIT_SIZE * sizeof(hash->hash[0]));
  for (i = 0; i < LHASH_INIT_SIZE; i++) {
    hash->hash[i].key = LUAV_NIL;
  }
  return hash;
}

void lhash_free(lhash_t *hash) {
  free(hash->hash);
  free(hash);
}

luav lhash_get(lhash_t *map, luav key) {
  u32 i;
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

void lhash_set(lhash_t *map, luav key, luav value) {
  assert(key != LUAV_NIL);

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
      break;
    }
  }
}

static void lhash_resize(lhash_t *map) {
  u32 i, end = map->cap;
  map->cap *= 2;
  map->cap += 1;
  lpair_t *old = map->hash;
  map->hash = xmalloc(map->cap * sizeof(map->hash[0]));

  for (i = 0; i < map->cap; i++) {
    map->hash[i].key = LUAV_NIL;
  }

  for (i = 0; i < end; i++) {
    if (old[i].key != LUAV_NIL) {
      lhash_set(map, old[i].key, old[i].value);
    }
  }
}
