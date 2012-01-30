#include <string.h>
#include <stdio.h>

#include "lhash.h"
#include "vm.h"

static lhash_t lua_string;
static luav lua_string_format(u32 argc, luav *argv);
static luav lua_string_rep(luav string, luav n);

static LUAF_VARARG(lua_string_format);
static LUAF_2ARG(lua_string_rep);

INIT static void lua_string_init() {
  lhash_init(&lua_string);
  lhash_set(&lua_string, LSTR("format"), lv_function(&lua_string_format_f));
  lhash_set(&lua_string, LSTR("rep"),    lv_function(&lua_string_rep_f));

  lhash_set(&lua_globals, LSTR("string"), lv_table(&lua_string));
}

DESTROY static void lua_string_destroy() {
  lhash_free(&lua_string);
}

static luav lua_string_format(u32 argc, luav *argv) {
  /* TODO: actually implement this */
  return argv[0];
}

static luav lua_string_rep(luav string, luav _n) {
  lstring_t *str = lstr_get(lv_getstring(string));
  size_t n = (size_t) lv_getnumber(_n);
  size_t len = n * str->length;

  char *newstr = xmalloc(len);
  char *ptr = newstr;
  while (n-- > 0) {
    memcpy(ptr, str->ptr, str->length);
    ptr += str->length;
  }

  return lv_string(lstr_add(newstr, len, TRUE));
}
