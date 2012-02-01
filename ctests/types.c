#include <assert.h>
#include <math.h>

#include "lhash.h"
#include "luav.h"
#include "vm.h"

int same(double a, double b) {
  return fabs(a - b) < 0.01;
}

int main() {
  /** NUMBERS **/
  luav one = lv_number(1);
  assert(same(lv_cvt(one), 1.0));
  assert(lv_gettype(one) == LNUMBER);
  assert(lv_isnumber(one));

  luav two = lv_number(2);
  assert(same(lv_castnumber(two, 0), 2.0));
  assert(lv_gettype(two) == LNUMBER);
  assert(lv_isnumber(two));

  luav nan = lv_number(NAN);
  assert(lv_bits(lv_castnumber(nan, 0)) == lv_bits(NAN));
  assert(isnan(lv_castnumber(nan, 0)));
  assert(lv_gettype(nan) == LNUMBER);
  assert(lv_isnumber(nan));

  luav inf = lv_number(1.0/0.0);
  assert(lv_bits(lv_castnumber(inf, 0)) == lv_bits(1.0/0.0));
  assert(isinf(lv_castnumber(inf, 0)));
  assert(lv_gettype(inf) == LNUMBER);
  assert(lv_isnumber(inf));

  luav neginf = lv_number(-1.0/0.0);
  assert(lv_bits(lv_castnumber(neginf, 0)) == lv_bits(-1.0/0.0));
  assert(isinf(lv_castnumber(neginf, 0)));
  assert(lv_gettype(neginf) == LNUMBER);
  assert(lv_isnumber(neginf));

  assert(lv_hash(one) != lv_hash(two));

  /** Booleans **/
  luav b_true1 = lv_bool(1);
  luav b_true2 = lv_bool(84);
  assert(b_true1 == b_true2);
  assert(isnan(lv_cvt(b_true1)));
  assert(isnan(lv_cvt(b_true2)));
  assert(lv_getbool(b_true1, 0) == 1);
  assert(lv_gettype(b_true1) == LBOOLEAN);
  assert(lv_gettype(b_true2) == LBOOLEAN);
  assert(lv_isbool(b_true1));
  assert(lv_isbool(b_true2));

  luav b_false = lv_bool(0);
  assert(b_true1 != b_false);
  assert(isnan(lv_cvt(b_false)));
  assert(lv_getbool(b_false, 0) == 0);
  assert(lv_gettype(b_false) == LBOOLEAN);
  assert(lv_isbool(b_false));

  assert(lv_hash(b_true1) != lv_hash(b_false));
  assert(lv_hash(b_true1) == lv_hash(b_true2));

  /** Nil **/
  luav b_nil = LUAV_NIL;
  assert(isnan(lv_cvt(b_nil)));
  assert(lv_gettype(b_nil) == LNIL);

  /** User data **/
  luav udata = lv_userdata((void*) 0x1234);
  assert(isnan(lv_cvt(udata)));
  assert(lv_getuserdata(udata, 0) == (void*) 0x1234);
  luav udata2 = lv_userdata((void*) 0x1235);
  assert(isnan(lv_cvt(udata2)));
  assert(lv_getuserdata(udata2, 0) == (void*) 0x1235);
  assert(udata != udata2);
  assert(lv_gettype(udata) == LUSERDATA);
  assert(lv_gettype(udata2) == LUSERDATA);
  assert(lv_isuserdata(udata));

  /** Tables **/
  lhash_t table1, table2;
  luav ltable1 = lv_table(&table1);
  luav ltable2 = lv_table(&table2);
  assert(isnan(lv_cvt(ltable1)) && isnan(lv_cvt(ltable2)));
  assert(ltable1 != ltable2);
  assert(lv_gettable(ltable1, 0) == &table1);
  assert(lv_gettable(ltable2, 0) == &table2);
  assert(lv_gettype(ltable1) == LTABLE);
  assert(lv_gettype(ltable2) == LTABLE);
  assert(lv_istable(ltable1));

  /** Upvalues **/
  upvalue_t up = {0, lv_number(1)};
  luav upvalue = lv_upvalue(&up);
  assert(lv_getupvalue(upvalue)->value == lv_number(1));
  assert(lv_castnumber(lv_getupvalue(upvalue)->value, 0) == 1);
  assert(lv_isupvalue(upvalue));

  /** Strings **/
  luav s1 = LSTR("asdf");
  luav s2 = LSTR("fo");
  assert(isnan(lv_cvt(s1)));
  assert(s1 != s2);
  assert(s1 == LSTR("asdf"));
  assert(lv_gettype(s1) == LSTRING);
  assert(lv_caststring(s1, 0) == lstr_get(lstr_add("asdf", 4, 0)));
  assert(lv_isstring(s1));

  /** Functions **/
  lclosure_t function;
  luav f1 = lv_function(&function);
  assert(isnan(lv_cvt(f1)));
  assert(lv_getfunction(f1, 0) == &function);
  assert(lv_isfunction(f1));

  /** Comparisons **/
  assert(lv_compare(lv_number(1), lv_number(2)) < 0);
  assert(lv_compare(lv_number(2), lv_number(1)) > 0);
  assert(lv_compare(lv_number(1), lv_number(1)) == 0);

  assert(lv_compare(LSTR("a"), LSTR("b")) < 0);
  assert(lv_compare(LSTR("a"), LSTR("ab")) < 0);
  assert(lv_compare(LSTR("b"), LSTR("a")) > 0);
  assert(lv_compare(LSTR("ab"), LSTR("a")) > 0);
  assert(lv_compare(LSTR("a"), LSTR("a")) == 0);

  return 0;
}
