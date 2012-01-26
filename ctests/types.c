#include <assert.h>
#include <math.h>

#include "lhash.h"
#include "luav.h"

int same(double a, double b) {
  return fabs(a - b) < 0.01;
}

int main() {
  lstr_init();
  luav upvalue;

  /** NUMBERS **/
  luav one = lv_number(1);
  assert(same(lv_cvt(one), 1.0));
  assert(lv_gettype(one) == LNUMBER);

  luav two = lv_number(2);
  assert(same(lv_getnumber(two), 2.0));
  assert(lv_gettype(two) == LNUMBER);

  luav nan = lv_number(NAN);
  assert(lv_bits(lv_getnumber(nan)) == lv_bits(NAN));
  assert(isnan(lv_getnumber(nan)));
  assert(lv_gettype(nan) == LNUMBER);

  luav inf = lv_number(1.0/0.0);
  assert(lv_bits(lv_getnumber(inf)) == lv_bits(1.0/0.0));
  assert(isinf(lv_getnumber(inf)));
  assert(lv_gettype(inf) == LNUMBER);

  assert(lv_hash(one) != lv_hash(two));
  upvalue = lv_upvalue(one);
  assert(lv_getnumber(upvalue) == 1);

  /** Booleans **/
  luav b_true1 = lv_bool(1);
  luav b_true2 = lv_bool(84);
  assert(b_true1 == b_true2);
  assert(isnan(lv_cvt(b_true1)));
  assert(isnan(lv_cvt(b_true2)));
  assert(lv_getbool(b_true1) == 1);
  assert(lv_gettype(b_true1) == LBOOLEAN);
  assert(lv_gettype(b_true2) == LBOOLEAN);
  upvalue = lv_upvalue(b_true1);
  assert(lv_getbool(upvalue) == 1);

  luav b_false = lv_bool(0);
  assert(b_true1 != b_false);
  assert(isnan(lv_cvt(b_false)));
  assert(lv_getbool(b_false) == 0);
  assert(lv_gettype(b_false) == LBOOLEAN);

  assert(lv_hash(b_true1) != lv_hash(b_false));
  assert(lv_hash(b_true1) == lv_hash(b_true2));

  /** Nil **/
  luav b_nil = LUAV_NIL;
  assert(isnan(lv_cvt(b_nil)));
  assert(lv_gettype(b_nil) == LNIL);

  /** User data **/
  luav udata = lv_userdata((void*) 0x1234);
  assert(isnan(lv_cvt(udata)));
  assert(lv_getuserdata(udata) == (void*) 0x1234);
  luav udata2 = lv_userdata((void*) 0x1235);
  assert(isnan(lv_cvt(udata2)));
  assert(lv_getuserdata(udata2) == (void*) 0x1235);
  assert(udata != udata2);
  assert(lv_gettype(udata) == LUSERDATA);
  assert(lv_gettype(udata2) == LUSERDATA);
  upvalue = lv_upvalue(udata);
  assert(lv_getuserdata(upvalue) == (void*) 0x1234);

  /** Tables **/
  lhash_t table1, table2;
  luav ltable1 = lv_table(&table1);
  luav ltable2 = lv_table(&table2);
  assert(isnan(lv_cvt(ltable1)) && isnan(lv_cvt(ltable2)));
  assert(ltable1 != ltable2);
  assert(lv_gettable(ltable1) == &table1);
  assert(lv_gettable(ltable2) == &table2);
  assert(lv_gettype(ltable1) == LTABLE);
  assert(lv_gettype(ltable2) == LTABLE);
  upvalue = lv_upvalue(ltable1);
  assert(lv_gettable(upvalue) == &table1);

  /** Upvalues **/
  luav u1 = lv_upvalue(lv_number(1));
  assert(isnan(lv_cvt(u1)));
  assert(lv_getupvalue(u1) == lv_number(1));
  assert(lv_getnumber(u1) == 1);

  /** Strings **/
  luav s1 = LSTR("asdf");
  luav s2 = LSTR("fo");
  assert(isnan(lv_cvt(s1)));
  assert(s1 != s2);
  assert(s1 == LSTR("asdf"));
  assert(lv_gettype(s1) == LSTRING);
  upvalue = lv_upvalue(s1);
  assert(lv_getstring(s1) == lv_getstring(s1));

  return 0;
}
