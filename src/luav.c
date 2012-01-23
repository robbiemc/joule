#include "config.h"
#include "luav.h"

luav lv_bool(u8 v) {
  return lv_cvt(LUAV_SETDATA(LBOOLEAN, !!v));
}

luav lv_number(u64 v) {
  return lv_cvt(v);
}

luav lv_string(lstring_t *v) {
  return lv_cvt(LUAV_SETDATA(LSTRING, (u64) v));
}

luav lv_nil() {
  return lv_cvt(LNIL);
}
