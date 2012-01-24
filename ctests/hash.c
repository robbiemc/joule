#include <assert.h>

#include "lhash.h"

int main() {
  lhash_t *map = lhash_alloc();
  assert(map != NULL);
  luav one = lv_number(1.0);

  assert(lhash_get(map, LUAV_NIL) == LUAV_NIL);
  assert(lhash_get(map, one) == LUAV_NIL);

  return 0;
}
