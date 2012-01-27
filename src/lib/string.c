#include "lhash.h"
#include "vm.h"

static lhash_t lua_string;
static luav lua_string_format(u32 argc, luav *argv);
static LUAF_VARARG(lua_string_format);

INIT static void lua_string_init() {
  lhash_init(&lua_string);
  lhash_set(&lua_string, LSTR("format"), lv_function(&lua_string_format_f));
  lhash_set(&lua_globals, LSTR("string"), lv_table(&lua_string));
}

DESTROY static void lua_string_destroy() {
  lhash_free(&lua_string);
}

static luav lua_string_format(u32 argc, luav *argv) {
  return argv[0];
}
