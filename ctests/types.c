#include <assert.h>
#include <math.h>

#include "luav.h"

int main() {
  /* TODO: test infinity is interpreted as a number */
  /* TODO: test machine NaN is interpreted as a number */
  luav one = lv_number(1);
  assert(one == lv_bits(1.0));
  assert(!isnan(lv_cvt(one)));

  luav two = lv_number(2);
  assert(two == lv_bits(2.0));
  assert(!isnan(lv_cvt(two)));

  assert(lv_hash(one) != lv_hash(two));

  luav b_true1 = lv_bool(1);
  luav b_true2 = lv_bool(84);
  assert(b_true1 == b_true2);
  assert(isnan(lv_cvt(b_true1)));
  assert(isnan(lv_cvt(b_true2)));

  luav b_false = lv_bool(0);
  assert(b_true1 != b_false);
  assert(isnan(lv_cvt(b_false)));

  assert(lv_hash(b_true1) != lv_hash(b_false));
  assert(lv_hash(b_true1) == lv_hash(b_true2));

  luav b_nil = LUAV_NIL;
  assert(isnan(lv_cvt(b_nil)));

  return 0;
}