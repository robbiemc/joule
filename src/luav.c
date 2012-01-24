#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "config.h"
#include "luav.h"

luav lv_bool(u8 v) {
  return LUAV_SETDATA(LBOOLEAN, !!v);
}

luav lv_number(u64 v) {
  return v;
}

luav lv_string(lstring_t *v) {
  return LUAV_SETDATA(LSTRING, (u64) v);
}

luav lv_nil() {
  return LUAV_NIL;
}

u32 lv_hash(luav value) {
  return (u32) (value >> LUAV_TYPE_MASK);
}

void lv_dump(luav value) {
  if ((value & LUAV_NAN_MASK) == LUAV_NAN_MASK) {
    printf("%f\n", lv_cvt(value));
    return;
  }

  u64 data = LUAV_DATA(value);

  switch (value & LUAV_TYPE_MASK) {
    case LNUMBER:     assert(0 && "LNUMBER souldn't exist really?");
    case LSTRING:     printf("{string...}");                return;
    case LTABLE:      printf("{string...}");                return;
    case LBOOLEAN:    printf(data ? "true\n" : "false\n");  return;
    case LNIL:        printf("nil\n");                      return;
  }

  printf("Bad luav type: %lld\n", value & LUAV_TYPE_MASK);
  assert(0 && "Bad luav type");
}
