#include <assert.h>
#include <math.h>

#include "config.h"
#include "luav.h"

static inline i64 extend(u64 bits) {
  struct { i64 x:48; } s;
  return s.x = bits;
}

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

int lv_hash(luav value) {
  u64 bits = lv_bits(value);

  if (isfinite(value)) {
    return bits >> LUAV_TYPE_BITS;
  }

  u64 data = LUAV_DATA(bits);

  switch (bits & LUAV_TYPE_MASK) {
    case LNUMBER:     return data;
    case LSTRING:     return data;
    case LTABLE:      return data;
    case LBOOLEAN:    return data + 1;
    case LNIL:        return 0;
  }

  assert(0 && "Bad luav type");
  return -1;
}
