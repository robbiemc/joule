#include "debug.h"
#include "lhash.h"
#include "lstring.h"
#include "panic.h"
#include "vm.h"

static lhash_t lua_io;
static luav lua_io_write(u32 argc, luav *argv);
static LUAF_VARARG(lua_io_write);

INIT static void lua_io_init() {
  lhash_init(&lua_io);
  lhash_set(&lua_io, LSTR("write"), lv_function(&lua_io_write_f));
  lhash_set(&lua_globals, LSTR("io"), lv_table(&lua_io));
}

DESTROY static void lua_io_destroy() {
  lhash_free(&lua_io);
}

static luav lua_io_write(u32 argc, luav *argv) {
  lstring_t *str;

  while (argc-- > 0) {
    luav value = *argv++;

    switch (lv_gettype(value)) {
      case LSTRING:
        str = lstr_get(lv_getstring(value));
        printf("%.*s", (int) str->length, str->ptr);
        break;

      case LNUMBER:
        printf(LUA_NUMBER_FMT, lv_getnumber(value));
        break;

      default:
        panic("Bad type in io.write");
    }
  }

  return LUAV_TRUE;
}
