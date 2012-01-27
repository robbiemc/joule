#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "lhash.h"
#include "luav.h"
#include "vm.h"

static luav str_number;
static luav str_nil;
static luav str_boolean;
static luav str_string;
static luav str_table;
static luav str_function;
static luav str_userdata;
static luav lua_type(luav v);
static luav lua_tostring(luav v);
static u32  lua_print(u32 argc, luav *argv, u32 retc, luav *retv);

static LUAF_1ARG(lua_type);
static LUAF_1ARG(lua_tostring);
static LUAF_VARARG(lua_print);

INIT static void lua_utils_init() {
  str_number   = LSTR("number");
  str_nil      = LSTR("nil");
  str_boolean  = LSTR("boolean");
  str_string   = LSTR("string");
  str_table    = LSTR("table");
  str_function = LSTR("function");
  str_userdata = LSTR("userdata");

  lhash_set(&lua_globals, LSTR("type"),     lv_function(&lua_type_f));
  lhash_set(&lua_globals, LSTR("tostring"), lv_function(&lua_tostring_f));
  lhash_set(&lua_globals, LSTR("print"),    lv_function(&lua_print_f));
}

static luav lua_type(luav v) {
  switch (lv_gettype(v)) {
    case LNUMBER:   return str_number;
    case LNIL:      return str_nil;
    case LBOOLEAN:  return str_boolean;
    case LSTRING:   return str_string;
    case LTABLE:    return str_table;
    case LFUNCTION: return str_function;
    case LUSERDATA: return str_userdata;
  }

  printf("Unknown luav: 0x%016" PRIu64, v);
  abort();
}

static luav lua_tostring(luav v) {
  /* TODO: if v has a metatable, call __tostring method */
  char *strbuf = xmalloc(100);

  switch (lv_gettype(v)) {
    case LNIL:      sprintf(strbuf, "nil");                               break;
    case LBOOLEAN:  sprintf(strbuf, lv_getbool(v) ? "true" : "false");    break;
    case LSTRING:   return v;
    case LTABLE:    sprintf(strbuf, "table: %p", lv_gettable(v));         break;
    case LFUNCTION: sprintf(strbuf, "function: %p", lv_getfunction(v));   break;
    case LUSERDATA: sprintf(strbuf, "userdata: %p", lv_getuserdata(v));   break;

    case LNUMBER:
      snprintf(strbuf, sizeof(strbuf), "%.10g", lv_getnumber(v));
      break;

    default:
      printf("Unknown luav: 0x%016" PRIu64, v);
      abort();
  }

  return lv_string(lstr_add(strbuf, strlen(strbuf), TRUE));
}

static u32 lua_print(u32 argc, luav *argv, u32 retc, luav *retv) {
  while (argc-- > 0) {
    luav arg = lua_tostring(*argv++);
    lstring_t *str = lstr_get(lv_getstring(arg));
    printf("%.*s", (int) str->length, str->ptr);
    if (argc > 0) {
      printf("\t"); /* TODO: is this right?! */
    }
  }
  printf("\n");
  return 0;
}
