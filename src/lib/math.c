#include <stdlib.h>
#include <limits.h>

#include "lhash.h"
#include "vm.h"

static lhash_t lua_math;
static luav lua_math_random(u32 argc, luav *argv);
static LUAF_VARARG(lua_math_random);

INIT static void lua_math_init() {
  lhash_init(&lua_math);
  lhash_set(&lua_math, LSTR("random"), lv_function(&lua_math_random_f));
  lhash_set(&lua_globals, LSTR("math"), lv_table(&lua_math));
}

DESTROY static void lua_math_destroy() {
  lhash_free(&lua_math);
}

static luav lua_math_random(u32 argc, luav *argv) {
  double num = ((double) random()) / ((double) LONG_MAX);
  double upper;
  double lower;

  switch (argc) {
    case 0:
      return lv_number(num);
    case 1:
      return lv_number(num * (lv_getnumber(argv[0]) - 1) + 1);

    default:
      upper = lv_getnumber(argv[1]);
      lower = lv_getnumber(argv[0]);
      return lv_number(num * (upper - lower) + lower);
  }
}
