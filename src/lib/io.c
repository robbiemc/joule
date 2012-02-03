#include <stdio.h>

#include "error.h"
#include "lhash.h"
#include "lstate.h"
#include "lstring.h"
#include "vm.h"

static lhash_t lua_io;
static u32 lua_io_write(LSTATE);
static LUAF(lua_io_write);

INIT static void lua_io_init() {
  lhash_init(&lua_io);
  REGISTER(&lua_io, "write", &lua_io_write_f);

  lhash_set(&lua_globals, LSTR("io"), lv_table(&lua_io));
}

DESTROY static void lua_io_destroy() {
  lhash_free(&lua_io);
}

static u32 lua_io_write(LSTATE) {
  lstring_t *str;
  u32 i;

  for (i = 0; i < argc; i++) {
    luav value = lstate_getval(i);

    switch (lv_gettype(value)) {
      case LSTRING:
        str = lv_caststring(value, i);
        printf("%.*s", (int) str->length, str->ptr);
        break;

      case LNUMBER:
        printf(LUA_NUMBER_FMT, lv_castnumber(value, i));
        break;

      default:
        err_badtype(i, LSTRING, lv_gettype(value));
    }
  }

  lstate_return1(LUAV_TRUE);
}
