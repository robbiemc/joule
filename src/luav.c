#include "config.h"
#include "luav.h"

const luav LV_NIL   = 0;
const luav LV_TRUE  = 1;
const luav LV_FALSE = 0;

luav lv_bool(u8 v) {
  return v ? LV_TRUE : LV_FALSE;
}

luav lv_number(u64 v) {
  return v;
}

luav lv_string(lstring_t *v) {
  return (luav) v;
}
