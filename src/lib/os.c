#include <time.h>

#include "lhash.h"
#include "vm.h"

static lhash_t lua_os;
static luav lua_os_clock();
static LUAF_0ARG(lua_os_clock);

INIT static void lua_os_init() {
  lhash_init(&lua_os);
  lhash_set(&lua_os, LSTR("clock"), lv_function(&lua_os_clock_f));
  lhash_set(&lua_globals, LSTR("os"), lv_table(&lua_os));
}

DESTROY static void lua_os_destroy() {
  lhash_free(&lua_os);
}

static luav lua_os_clock() {
  clock_t c = clock();
  return lv_number((double) c / CLOCKS_PER_SEC);
}
