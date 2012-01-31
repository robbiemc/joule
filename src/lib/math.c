#include <math.h>
#include <stdlib.h>

#include "lhash.h"
#include "lstate.h"
#include "vm.h"

static lhash_t lua_math;
static u32 lua_math_abs(LSTATE);
static u32 lua_math_acos(LSTATE);
static u32 lua_math_asin(LSTATE);
static u32 lua_math_atan(LSTATE);
static u32 lua_math_atan2(LSTATE);
static u32 lua_math_ceil(LSTATE);
static u32 lua_math_cos(LSTATE);
static u32 lua_math_cosh(LSTATE);
static u32 lua_math_deg(LSTATE);
static u32 lua_math_exp(LSTATE);
static u32 lua_math_floor(LSTATE);
static u32 lua_math_fmod(LSTATE);
static u32 lua_math_frexp(LSTATE);
static u32 lua_math_ldexp(LSTATE);
static u32 lua_math_log(LSTATE);
static u32 lua_math_log10(LSTATE);
static u32 lua_math_max(LSTATE);
static u32 lua_math_min(LSTATE);
static u32 lua_math_mod(LSTATE);
static u32 lua_math_modf(LSTATE);
static u32 lua_math_pow(LSTATE);
static u32 lua_math_rad(LSTATE);
static u32 lua_math_random(LSTATE);
static u32 lua_math_randomseed(LSTATE);
static u32 lua_math_sin(LSTATE);
static u32 lua_math_sinh(LSTATE);
static u32 lua_math_sqrt(LSTATE);
static u32 lua_math_tan(LSTATE);
static u32 lua_math_tanh(LSTATE);

static LUAF(lua_math_abs);
static LUAF(lua_math_acos);
static LUAF(lua_math_asin);
static LUAF(lua_math_atan);
static LUAF(lua_math_atan2);
static LUAF(lua_math_ceil);
static LUAF(lua_math_cos);
static LUAF(lua_math_cosh);
static LUAF(lua_math_deg);
static LUAF(lua_math_exp);
static LUAF(lua_math_floor);
static LUAF(lua_math_fmod);
static LUAF(lua_math_frexp);
static LUAF(lua_math_ldexp);
static LUAF(lua_math_log);
static LUAF(lua_math_log10);
static LUAF(lua_math_max);
static LUAF(lua_math_min);
static LUAF(lua_math_mod);
static LUAF(lua_math_modf);
static LUAF(lua_math_pow);
static LUAF(lua_math_rad);
static LUAF(lua_math_random);
static LUAF(lua_math_randomseed);
static LUAF(lua_math_sin);
static LUAF(lua_math_sinh);
static LUAF(lua_math_sqrt);
static LUAF(lua_math_tan);
static LUAF(lua_math_tanh);

INIT static void lua_math_init() {
  lhash_init(&lua_math);

  lhash_set(&lua_math, LSTR("pi"),    lv_number(M_PI));
  lhash_set(&lua_math, LSTR("huge"),  lv_number(HUGE_VAL));
  REGISTER(&lua_math, "abs",        &lua_math_abs_f);
  REGISTER(&lua_math, "acos",       &lua_math_acos_f);
  REGISTER(&lua_math, "asin",       &lua_math_asin_f);
  REGISTER(&lua_math, "atan",       &lua_math_atan_f);
  REGISTER(&lua_math, "atan2",      &lua_math_atan2_f);
  REGISTER(&lua_math, "ceil",       &lua_math_ceil_f);
  REGISTER(&lua_math, "cos",        &lua_math_cos_f);
  REGISTER(&lua_math, "cosh",       &lua_math_cosh_f);
  REGISTER(&lua_math, "deg",        &lua_math_deg_f);
  REGISTER(&lua_math, "exp",        &lua_math_exp_f);
  REGISTER(&lua_math, "floor",      &lua_math_floor_f);
  REGISTER(&lua_math, "fmod",       &lua_math_fmod_f);
  REGISTER(&lua_math, "frexp",      &lua_math_frexp_f);
  REGISTER(&lua_math, "ldexp",      &lua_math_ldexp_f);
  REGISTER(&lua_math, "log",        &lua_math_log_f);
  REGISTER(&lua_math, "log10",      &lua_math_log10_f);
  REGISTER(&lua_math, "min",        &lua_math_min_f);
  REGISTER(&lua_math, "max",        &lua_math_max_f);
  REGISTER(&lua_math, "mod",        &lua_math_mod_f);
  REGISTER(&lua_math, "modf",       &lua_math_modf_f);
  REGISTER(&lua_math, "pow",        &lua_math_pow_f);
  REGISTER(&lua_math, "rad",        &lua_math_rad_f);
  REGISTER(&lua_math, "random",     &lua_math_random_f);
  REGISTER(&lua_math, "randomseed", &lua_math_randomseed_f);
  REGISTER(&lua_math, "sin",        &lua_math_sin_f);
  REGISTER(&lua_math, "sinh",       &lua_math_sinh_f);
  REGISTER(&lua_math, "sqrt",       &lua_math_sqrt_f);
  REGISTER(&lua_math, "tan",        &lua_math_tan_f);
  REGISTER(&lua_math, "tanh",       &lua_math_tanh_f);

  lhash_set(&lua_globals, LSTR("math"), lv_table(&lua_math));
}

DESTROY static void lua_math_destroy() {
  lhash_free(&lua_math);
}

static u32 lua_math_abs(LSTATE) {
  lstate_return1(lv_number(fabs(lstate_getnumber(0))));
}

static u32 lua_math_acos(LSTATE) {
  lstate_return1(lv_number(acos(lstate_getnumber(0))));
}

static u32 lua_math_asin(LSTATE) {
  lstate_return1(lv_number(asin(lstate_getnumber(0))));
}

static u32 lua_math_atan(LSTATE) {
  lstate_return1(lv_number(atan(lstate_getnumber(0))));
}

static u32 lua_math_atan2(LSTATE) {
  lstate_return1(lv_number(atan2(lstate_getnumber(0), lstate_getnumber(1))));
}

static u32 lua_math_ceil(LSTATE) {
  lstate_return1(lv_number(ceil(lstate_getnumber(0))));
}

static u32 lua_math_cos(LSTATE) {
  lstate_return1(lv_number(cos(lstate_getnumber(0))));
}

static u32 lua_math_cosh(LSTATE) {
  lstate_return1(lv_number(cosh(lstate_getnumber(0))));
}

static u32 lua_math_deg(LSTATE) {
  lstate_return1(lv_number(lstate_getnumber(0) * 180.0 / M_PI));
}

static u32 lua_math_exp(LSTATE) {
  lstate_return1(lv_number(exp(lstate_getnumber(0))));
}

static u32 lua_math_floor(LSTATE) {
  lstate_return1(lv_number(floor(lstate_getnumber(0))));
}

static u32 lua_math_fmod(LSTATE) {
  lstate_return1(lv_number(fmod(lstate_getnumber(0), lstate_getnumber(1))));
}

static u32 lua_math_frexp(LSTATE) {
  int exp;
  double mantissa = frexp(lstate_getnumber(0), &exp);
  lstate_return(lv_number(mantissa), 0);
  lstate_return(lv_number((double) exp), 1);
  return 2;
}

static u32 lua_math_ldexp(LSTATE) {
  lstate_return1(lv_number(ldexp(lstate_getnumber(0),
                                 (int) lstate_getnumber(1))));
}

static u32 lua_math_log(LSTATE) {
  lstate_return1(lv_number(log(lstate_getnumber(0))));
}

static u32 lua_math_log10(LSTATE) {
  lstate_return1(lv_number(log10(lstate_getnumber(0))));
}

static u32 lua_math_max(LSTATE) {
  u32 i;
  double max = lstate_getnumber(0);
  for (i = 1; i < argc; i++) {
    double v = lstate_getnumber(i);
    max = v > max ? v : max;
  }
  lstate_return1(lv_number(max));
}

static u32 lua_math_min(LSTATE) {
  u32 i;
  double min = lstate_getnumber(0);
  for (i = 1; i < argc; i++) {
    double v = lstate_getnumber(i);
    min = v < min ? v : min;
  }
  lstate_return1(lv_number(min));
}

static u32 lua_math_mod(LSTATE) {
  double x = lstate_getnumber(0);
  double y = lstate_getnumber(1);
  lstate_return1(lv_number(fmod(x, y)));
}

static u32 lua_math_modf(u32 argc, luav *argv, u32 retc, luav *retv) {
  double ipart;
  double fpart = modf(lstate_getnumber(0), &ipart);
  lstate_return(lv_number(ipart), 0);
  lstate_return(lv_number(fpart), 1);
  return 2;
}

static u32 lua_math_pow(LSTATE) {
  lstate_return1(lv_number(pow(lstate_getnumber(0), lstate_getnumber(1))));
}

static u32 lua_math_rad(LSTATE) {
  lstate_return1(lv_number(lstate_getnumber(0) * M_PI / 180.0));
}

static u32 lua_math_random(LSTATE) {
  double num = ((double) rand()) / ((double) RAND_MAX);
  double upper;
  double lower;

  switch (argc) {
    case 0:
      lstate_return1(lv_number(num));
    case 1:
      num = num * lstate_getnumber(0) + 1;
      break;

    default:
      upper = lstate_getnumber(1);
      lower = lstate_getnumber(0);
      if (lower > upper) {
        err_str(1, "interval is empty");
      }
      num = num * (upper - lower + 1) + lower;
      break;
  }

  lstate_return1(lv_number(floor(num)));
}

static u32 lua_math_randomseed(LSTATE) {
  srand((u32) lstate_getnumber(0));
  return 0;
}

static u32 lua_math_sin(LSTATE) {
  lstate_return1(lv_number(sin(lstate_getnumber(0))));
}

static u32 lua_math_sinh(LSTATE) {
  lstate_return1(lv_number(sinh(lstate_getnumber(0))));
}

static u32 lua_math_sqrt(LSTATE) {
  lstate_return1(lv_number(sqrt(lstate_getnumber(0))));
}

static u32 lua_math_tan(LSTATE) {
  lstate_return1(lv_number(tan(lstate_getnumber(0))));
}

static u32 lua_math_tanh(LSTATE) {
  lstate_return1(lv_number(tanh(lstate_getnumber(0))));
}
