#include <assert.h>
#include <math.h>

#include "lhash.h"
#include "luav.h"

int same(double a, double b) {
  return fabs(a - b) < 0.01;
}

int main() {
  /** NUMBERS **/
  luav one = lv_number(1);
  assert(same(lv_cvt(one), 1.0));

  luav two = lv_number(2);
  assert(same(lv_getnumber(two), 2.0));

  luav nan = lv_number(0.0/0.0);
  assert(lv_bits(lv_getnumber(nan)) == lv_bits(0.0/0.0));
  assert(isnan(lv_getnumber(nan)));

  luav inf = lv_number(1.0/0.0);
  assert(lv_bits(lv_getnumber(inf)) == lv_bits(1.0/0.0));
  assert(isinf(lv_getnumber(inf)));

  assert(lv_hash(one) != lv_hash(two));

  /** Booleans **/
  luav b_true1 = lv_bool(1);
  luav b_true2 = lv_bool(84);
  assert(b_true1 == b_true2);
  assert(isnan(lv_cvt(b_true1)));
  assert(isnan(lv_cvt(b_true2)));
  assert(lv_getbool(b_true1) == 1);

  luav b_false = lv_bool(0);
  assert(b_true1 != b_false);
  assert(isnan(lv_cvt(b_false)));
  assert(lv_getbool(b_false) == 0);

  assert(lv_hash(b_true1) != lv_hash(b_false));
  assert(lv_hash(b_true1) == lv_hash(b_true2));

  /** Nil **/
  luav b_nil = LUAV_NIL;
  assert(isnan(lv_cvt(b_nil)));

  /** User data **/
  luav udata = lv_userdata((void*) 0x1234);
  assert(isnan(lv_cvt(udata)));
  assert(lv_getuserdata(udata) == (void*) 0x1234);
  luav udata2 = lv_userdata((void*) 0x1235);
  assert(isnan(lv_cvt(udata2)));
  assert(lv_getuserdata(udata2) == (void*) 0x1235);
  assert(udata != udata2);

  /** Tables **/
  lhash_t table1, table2;
  luav ltable1 = lv_table(&table1);
  luav ltable2 = lv_table(&table2);
  assert(isnan(lv_cvt(ltable1)) && isnan(lv_cvt(ltable2)));
  assert(ltable1 != ltable2);
  assert(lv_gettable(ltable1) == &table1);
  assert(lv_gettable(ltable2) == &table2);

  return 0;
}
