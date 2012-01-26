#include "debug.h"
#include "lhash.h"
#include "vm.h"

static lhash_t lua_io;
static u32 lua_io_write(u32 argc, luav *argv, u32 retc, luav *retv);
static LUAF_VARARG(lua_io_write);

INIT static void init_io() {
  lhash_init(&lua_io);
  lhash_set(&lua_io, LSTR("write"), lv_function(&lua_io_write_f));
  lhash_set(&lua_globals, LSTR("io"), lv_table(&lua_io));
}

static u32 lua_io_write(u32 argc, luav *argv, u32 retc, luav *retv) {
  while (argc-- > 0) {
    dbg_dump_luav(stdout, *argv++);
  }
  if (retc > 0) {
    *retv = lv_bool(1);
  }
  return !!retc;
}
