#include <stdlib.h>
#include <time.h>

#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "panic.h"
#include "vm.h"

static lhash_t lua_os;
static luav lua_os_clock(void);
static luav lua_os_exit(luav status);
static luav lua_os_execute(luav cmd);
static LUAF_0ARG(lua_os_clock);
static LUAF_1ARG(lua_os_exit);
static LUAF_1ARG(lua_os_execute);

INIT static void lua_os_init() {
  lhash_init(&lua_os);
  lhash_set(&lua_os, LSTR("clock"),   lv_function(&lua_os_clock_f));
  lhash_set(&lua_os, LSTR("exit"),    lv_function(&lua_os_exit_f));
  lhash_set(&lua_os, LSTR("execute"), lv_function(&lua_os_execute_f));

  lhash_set(&lua_globals, LSTR("os"), lv_table(&lua_os));
}

DESTROY static void lua_os_destroy() {
  lhash_free(&lua_os);
}

static luav lua_os_clock() {
  clock_t c = clock();
  return lv_number((double) c / CLOCKS_PER_SEC);
}

static luav lua_os_exit(luav status) {
  exit((int) lv_getnumber(lv_tonumber(status, 10)));
}

static luav lua_os_execute(luav cmd) {
  switch (lv_gettype(cmd)) {
    case LNIL:
    case LNUMBER: return lv_number(1);
    case LSTRING: break;
    default:      panic("bad type in execute: %d", lv_gettype(cmd));
  }

  lstring_t *str = lstr_get(lv_getstring(cmd));
  return lv_number(system(str->ptr));
}
