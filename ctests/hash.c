#include <assert.h>

#include "lhash.h"

int main() {
  lhash_t map;
  lhash_init(&map);
  luav one = lv_number(1.0);
  luav two = lv_number(2.0);

  assert(lhash_get(&map, LUAV_NIL) == LUAV_NIL);
  assert(lhash_get(&map, one) == LUAV_NIL);
  assert(lhash_get(&map, two) == LUAV_NIL);

  lhash_set(&map, one, two);
  assert(lhash_get(&map, LUAV_NIL) == LUAV_NIL);
  assert(lhash_get(&map, one) == two);
  assert(lhash_get(&map, two) == LUAV_NIL);

  lhash_set(&map, one, one);
  assert(lhash_get(&map, LUAV_NIL) == LUAV_NIL);
  assert(lhash_get(&map, one) == one);
  assert(lhash_get(&map, two) == LUAV_NIL);

  lhash_set(&map, two, one);
  assert(lhash_get(&map, LUAV_NIL) == LUAV_NIL);
  assert(lhash_get(&map, one) == one);
  assert(lhash_get(&map, two) == one);

  u32 i;
  for (i = 0; i < 1000000; i++) {
    lhash_set(&map, lv_number(i), lv_number(i + 1));
  }

  for (i = 0; i < 1000000; i++) {
    assert(lhash_get(&map, lv_number(i)) == lv_number(i + 1));
  }

  return 0;
}
