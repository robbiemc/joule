#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "lhash.h"
#include "luav.h"
#include "panic.h"
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
static luav lua_tonumber(u32 args, luav *argv);
static u32  lua_print(u32 argc, luav *argv, u32 retc, luav *retv);

static LUAF_1ARG(lua_type);
static LUAF_1ARG(lua_tostring);
static LUAF_VARRET(lua_print);
static LUAF_VARARG(lua_tonumber);

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
  lhash_set(&lua_globals, LSTR("tonumber"), lv_function(&lua_tonumber_f));
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

  panic("Unknown luav: 0x%016" PRIu64, v);
}

static luav lua_tostring(luav v) {
  /* TODO: if v has a metatable, call __tostring method */
  char *strbuf = xmalloc(LUAV_INIT_STRING);
  int len;

  switch (lv_gettype(v)) {
    case LNIL:
      len = sprintf(strbuf, "nil");
      break;
    case LSTRING:
      return v;
    case LTABLE:
      len = sprintf(strbuf, "table: %p", lv_gettable(v));
      break;
    case LFUNCTION:
      len = sprintf(strbuf, "function: %p", lv_getfunction(v));
      break;
    case LUSERDATA:
      len = sprintf(strbuf, "userdata: %p", lv_getuserdata(v));
      break;
    case LBOOLEAN:
      len = sprintf(strbuf, lv_getbool(v) ? "true" : "false");
      break;
    case LNUMBER: {
      len = snprintf(strbuf, LUAV_INIT_STRING, LUA_NUMBER_FMT, lv_getnumber(v));
      break;
    }

    default:
      panic("Unknown luav: 0x%016" PRIu64, v);
  }

  return lv_string(lstr_add(strbuf, (size_t) len, TRUE));
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

static luav lua_tonumber(u32 argc, luav *argv) {
  assert(argc > 0);
  u8 type = lv_gettype(argv[0]);
  if (type == LNUMBER) {
    return argv[0];
  } else if (type != LSTRING) {
    return LUAV_NIL;
  }

  int base;
  if (argc == 1) {
    base = 10;
  } else {
    double tmp;
    modf(lv_getnumber(argv[1]), &tmp);
    base = (int) tmp;
  }

  lstring_t *str = lstr_get(lv_getstring(argv[0]));
  char *end;
  double num;

  if (base == 10) {
    num = strtod(str->ptr, &end);
  } else {
    num = (double) strtoul(str->ptr, &end, base);
  }
  if (end == str->ptr + str->length - 1) {
    return lv_number(num);
  }
  return LUAV_NIL;
}
