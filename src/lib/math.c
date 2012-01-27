#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "lhash.h"
#include "vm.h"

static lhash_t lua_math;
static luav lua_math_abs(luav x);
static luav lua_math_acos(luav x);
static luav lua_math_asin(luav x);
static luav lua_math_atan(luav x);
static luav lua_math_atan2(luav x, luav y);
static luav lua_math_ceil(luav x);
static luav lua_math_cos(luav x);
static luav lua_math_cosh(luav x);
static luav lua_math_deg(luav x);
static luav lua_math_exp(luav x);
static luav lua_math_floor(luav x);
static luav lua_math_fmod(luav x, luav y);
static u32 lua_math_frexp(u32 argc, luav *argv, u32 retc, luav *retv);
static luav lua_math_ldexp(luav m, luav e);
static luav lua_math_log(luav x);
static luav lua_math_log10(luav x);
static luav lua_math_max(u32 argc, luav *argv);
static luav lua_math_min(u32 argc, luav *argv);
static u32 lua_math_modf(u32 argc, luav *argv, u32 retc, luav *retv);
static luav lua_math_pow(luav x, luav y);
static luav lua_math_rad(luav x);
static luav lua_math_random(u32 argc, luav *argv);
static u32 lua_math_randomseed(u32 argc, luav *argv, u32 retc, luav *retv);
static luav lua_math_sin(luav x);
static luav lua_math_sinh(luav x);
static luav lua_math_sqrt(luav x);
static luav lua_math_tan(luav x);
static luav lua_math_tanh(luav x);

static LUAF_1ARG(lua_math_abs);
static LUAF_1ARG(lua_math_acos);
static LUAF_1ARG(lua_math_asin);
static LUAF_1ARG(lua_math_atan);
static LUAF_2ARG(lua_math_atan2);
static LUAF_1ARG(lua_math_ceil);
static LUAF_1ARG(lua_math_cos);
static LUAF_1ARG(lua_math_cosh);
static LUAF_1ARG(lua_math_deg);
static LUAF_1ARG(lua_math_exp);
static LUAF_1ARG(lua_math_floor);
static LUAF_2ARG(lua_math_fmod);
static LUAF_VARRET(lua_math_frexp);
static LUAF_2ARG(lua_math_ldexp);
static LUAF_1ARG(lua_math_log);
static LUAF_1ARG(lua_math_log10);
static LUAF_VARARG(lua_math_max);
static LUAF_VARARG(lua_math_min);
static LUAF_VARRET(lua_math_modf);
static LUAF_2ARG(lua_math_pow);
static LUAF_1ARG(lua_math_rad);
static LUAF_VARARG(lua_math_random);
static LUAF_VARRET(lua_math_randomseed);
static LUAF_1ARG(lua_math_sin);
static LUAF_1ARG(lua_math_sinh);
static LUAF_1ARG(lua_math_sqrt);
static LUAF_1ARG(lua_math_tan);
static LUAF_1ARG(lua_math_tanh);

INIT static void lua_math_init() {
  lhash_init(&lua_math);
  lhash_set(&lua_math, LSTR("pi"), lv_number(M_PI));
  lhash_set(&lua_math, LSTR("huge"), lv_number(HUGE_VAL));
  lhash_set(&lua_math, LSTR("abs"), lv_function(&lua_math_abs_f));
  lhash_set(&lua_math, LSTR("acos"), lv_function(&lua_math_acos_f));
  lhash_set(&lua_math, LSTR("asin"), lv_function(&lua_math_asin_f));
  lhash_set(&lua_math, LSTR("atan"), lv_function(&lua_math_atan_f));
  lhash_set(&lua_math, LSTR("atan2"), lv_function(&lua_math_atan2_f));
  lhash_set(&lua_math, LSTR("ceil"), lv_function(&lua_math_ceil_f));
  lhash_set(&lua_math, LSTR("cos"), lv_function(&lua_math_cos_f));
  lhash_set(&lua_math, LSTR("cosh"), lv_function(&lua_math_cosh_f));
  lhash_set(&lua_math, LSTR("deg"), lv_function(&lua_math_deg_f));
  lhash_set(&lua_math, LSTR("exp"), lv_function(&lua_math_exp_f));
  lhash_set(&lua_math, LSTR("floor"), lv_function(&lua_math_floor_f));
  lhash_set(&lua_math, LSTR("fmod"), lv_function(&lua_math_fmod_f));
  lhash_set(&lua_math, LSTR("frexp"), lv_function(&lua_math_frexp_f));
  lhash_set(&lua_math, LSTR("ldexp"), lv_function(&lua_math_ldexp_f));
  lhash_set(&lua_math, LSTR("log"), lv_function(&lua_math_log_f));
  lhash_set(&lua_math, LSTR("log10"), lv_function(&lua_math_log10_f));
  lhash_set(&lua_math, LSTR("min"), lv_function(&lua_math_min_f));
  lhash_set(&lua_math, LSTR("max"), lv_function(&lua_math_max_f));
  lhash_set(&lua_math, LSTR("modf"), lv_function(&lua_math_modf_f));
  lhash_set(&lua_math, LSTR("pow"), lv_function(&lua_math_pow_f));
  lhash_set(&lua_math, LSTR("rad"), lv_function(&lua_math_rad_f));
  lhash_set(&lua_math, LSTR("random"), lv_function(&lua_math_random_f));
  lhash_set(&lua_math, LSTR("randomseed"), lv_function(&lua_math_randomseed_f));
  lhash_set(&lua_math, LSTR("sin"), lv_function(&lua_math_sin_f));
  lhash_set(&lua_math, LSTR("sinh"), lv_function(&lua_math_sinh_f));
  lhash_set(&lua_math, LSTR("sqrt"), lv_function(&lua_math_sqrt_f));
  lhash_set(&lua_math, LSTR("tan"), lv_function(&lua_math_tan_f));
  lhash_set(&lua_math, LSTR("tanh"), lv_function(&lua_math_tanh_f));
  lhash_set(&lua_globals, LSTR("math"), lv_table(&lua_math));
}

DESTROY static void lua_math_destroy() {
  lhash_free(&lua_math);
}

static luav lua_math_abs(luav x) {
  return lv_number(fabs(lv_getnumber(x)));
}

static luav lua_math_acos(luav x) {
  return lv_number(acos(lv_getnumber(x)));
}

static luav lua_math_asin(luav x) {
  return lv_number(asin(lv_getnumber(x)));
}

static luav lua_math_atan(luav x) {
  return lv_number(atan(lv_getnumber(x)));
}

static luav lua_math_atan2(luav x, luav y) {
  return lv_number(atan2(lv_getnumber(x), lv_getnumber(y)));
}

static luav lua_math_ceil(luav x) {
  return lv_number(ceil(lv_getnumber(x)));
}

static luav lua_math_cos(luav x) {
  return lv_number(cos(lv_getnumber(x)));
}

static luav lua_math_cosh(luav x) {
  return lv_number(cosh(lv_getnumber(x)));
}

static luav lua_math_deg(luav x) {
  return lv_number(lv_getnumber(x) * 180.0 / M_PI);
}

static luav lua_math_exp(luav x) {
  return lv_number(exp(lv_getnumber(x)));
}

static luav lua_math_floor(luav x) {
  return lv_number(floor(lv_getnumber(x)));
}

static luav lua_math_fmod(luav x, luav y) {
  return lv_number(fmod(lv_getnumber(x), lv_getnumber(y)));
}

static u32 lua_math_frexp(u32 argc, luav *argv, u32 retc, luav *retv) {
  assert(argc > 0);
  int exp;
  double mantissa = frexp(lv_getnumber(argv[0]), &exp);
  u32 ret = 0;
  if (retc > 0) {
    retv[0] = lv_number(mantissa);
    ret++;
  }
  if (retc > 1) {
    retv[1] = lv_number((double) exp);
    ret++;
  }
  return ret;
}

static luav lua_math_ldexp(luav m, luav e) {
  return lv_number(ldexp(lv_getnumber(m), (int) lv_getnumber(e)));
}

static luav lua_math_log(luav x) {
  return lv_number(log(lv_getnumber(x)));
}

static luav lua_math_log10(luav x) {
  return lv_number(log10(lv_getnumber(x)));
}

static luav lua_math_max(u32 argc, luav *argv) {
  u32 i;
  assert(argc > 0);
  double max = lv_getnumber(argv[0]);
  for (i = 1; i < argc; i++) {
    double v = lv_getnumber(argv[i]);
    max = v > max ? v : max;
  }
  return lv_number(max);
}

static luav lua_math_min(u32 argc, luav *argv) {
  u32 i;
  assert(argc > 0);
  double min = lv_getnumber(argv[0]);
  for (i = 1; i < argc; i++) {
    double v = lv_getnumber(argv[i]);
    min = v < min ? v : min;
  }
  return lv_number(min);
}

static u32 lua_math_modf(u32 argc, luav *argv, u32 retc, luav *retv) {
  assert(argc > 0);
  double ipart;
  double fpart = modf(lv_getnumber(argv[0]), &ipart);
  u32 ret = 0;
  if (retc > 0) {
    retv[0] = lv_number(ipart);
    ret++;
  }
  if (retc > 1) {
    retv[1] = lv_number(fpart);
    ret++;
  }
  return ret;
}

static luav lua_math_pow(luav x, luav y) {
  return lv_number(pow(lv_getnumber(x), lv_getnumber(y)));
}

static luav lua_math_rad(luav x) {
  return lv_number(lv_getnumber(x) * M_PI / 180.0);
}

static luav lua_math_random(u32 argc, luav *argv) {
  double num = ((double) rand()) / ((double) RAND_MAX);
  double upper;
  double lower;

  switch (argc) {
    case 0:
      return lv_number(num);
    case 1:
      num = num * (lv_getnumber(argv[0]) - 1) + 1;
      break;

    default:
      upper = lv_getnumber(argv[1]);
      lower = lv_getnumber(argv[0]);
      num = num * (upper - lower) + lower;
      break;
  }

  modf(num, &num);
  return lv_number(num);
}

static u32 lua_math_randomseed(u32 argc, luav *argv, u32 retc, luav *retv) {
  assert(argc > 0);
  srand((u32) lv_getnumber(argv[0]));
  return 0;
}

static luav lua_math_sin(luav x) {
  return lv_number(sin(lv_getnumber(x)));
}

static luav lua_math_sinh(luav x) {
  return lv_number(sinh(lv_getnumber(x)));
}

static luav lua_math_sqrt(luav x) {
  return lv_number(sqrt(lv_getnumber(x)));
}

static luav lua_math_tan(luav x) {
  return lv_number(tan(lv_getnumber(x)));
}

static luav lua_math_tanh(luav x) {
  return lv_number(tanh(lv_getnumber(x)));
}
