#include "util.h"
#include "vm.h"

const lvalue LV_NIL   = 0;
const lvalue LV_TRUE  = 1;
const lvalue LV_FALSE = 0;

lvalue lv_bool(u8 v) {
  return v ? LV_TRUE : LV_FALSE;
}

lvalue lv_number(u64 v) {
  return v;
}

lvalue lv_string(lstring_t *v) {
  return (lvalue) v;
}
