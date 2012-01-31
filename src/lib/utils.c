#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "lhash.h"
#include "luav.h"
#include "meta.h"
#include "panic.h"
#include "vm.h"

static luav str_number;
static luav str_nil;
static luav str_boolean;
static luav str_string;
static luav str_table;
static luav str_function;
static luav str_userdata;
static luav str_hash;
static luav str_metatable;
static u32  lua_assert(u32 argc, luav *argv, u32 retc, luav *retv);
static luav lua_type(luav v);
static luav lua_tostring(luav v);
static luav lua_tonumber(luav num, luav base);
static u32  lua_print(u32 argc, luav *argv, u32 retc, luav *retv);
static u32  lua_select(u32 argc, luav *argv, u32 retc, luav *retv);
static luav lua_rawget(luav table, luav key);
static luav lua_setmetatable(luav table, luav meta);
static luav lua_getmetatable(luav table);

static LUAF_VARRET(lua_assert);
static LUAF_1ARG(lua_type);
static LUAF_1ARG(lua_tostring);
static LUAF_VARRET(lua_print);
static LUAF_VARRET(lua_select);
static LUAF_2ARG(lua_tonumber);
static LUAF_2ARG(lua_rawget);
static LUAF_2ARG(lua_setmetatable);
static LUAF_1ARG(lua_getmetatable);

INIT static void lua_utils_init() {
  str_number    = LSTR("number");
  str_nil       = LSTR("nil");
  str_boolean   = LSTR("boolean");
  str_string    = LSTR("string");
  str_table     = LSTR("table");
  str_function  = LSTR("function");
  str_userdata  = LSTR("userdata");
  str_hash      = LSTR("#");
  str_metatable = LSTR("__metatable");

  REGISTER(&lua_globals, "assert",        &lua_assert_f);
  REGISTER(&lua_globals, "type",          &lua_type_f);
  REGISTER(&lua_globals, "tostring",      &lua_tostring_f);
  REGISTER(&lua_globals, "print",         &lua_print_f);
  REGISTER(&lua_globals, "tonumber",      &lua_tonumber_f);
  REGISTER(&lua_globals, "select",        &lua_select_f);
  REGISTER(&lua_globals, "rawget",        &lua_rawget_f);
  REGISTER(&lua_globals, "setmetatable",  &lua_setmetatable_f);
  REGISTER(&lua_globals, "getmetatable",  &lua_getmetatable_f);
}

static u32 lua_assert(u32 argc, luav *argv, u32 retc, luav *retv) {
  assert(argc > 0);
  luav boolean = lv_tobool(argv[0]);
  if (boolean == LUAV_FALSE) {
    luav msg = argc > 1 ? argv[1] : LSTR("assertion failed!");
    lua_print(1, &msg, 0, NULL);
    assert(0);
  } else {
    u32 i;
    for (i = 0; i < argc && i < retc; i++) {
      retv[i] = argv[i];
    }
    return i;
  }
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

static luav lua_tonumber(luav number, luav _base) {
  int base = 10;
  if (_base != LUAV_NIL) {
    base = (int) lv_getnumber(lv_tonumber(_base, 10));
  }
  return lv_tonumber(number, base);
}

static u32 lua_select(u32 argc, luav *argv, u32 retc, luav *retv) {
  assert(argc > 0);
  if (argv[0] == str_hash) {
    if (retc > 0) {
      retv[0] = lv_number(argc - 1);
    }
    return 1;
  }

  u32 i, off = (u32) lv_getnumber(argv[0]);
  for (i = 0; i < retc && i + off < argc; i++) {
    retv[i] = argv[i + off];
  }
  return i;
}

static luav lua_rawget(luav table, luav key) {
  return lhash_get(lv_gettable(table), key);
}

static luav lua_setmetatable(luav table, luav meta) {
  assert(lv_gettype(table) == LTABLE);
  // make sure the current metatable isn't protected
  lhash_t *old = lv_gettable(table)->metatable;
  assert(old == NULL || old->metamethods[META_METATABLE] == LUAV_NIL);

  switch (lv_gettype(meta)) {
    case LNIL:
      lv_gettable(table)->metatable = NULL;
      break;
    case LTABLE:
      lv_gettable(table)->metatable = lv_gettable(meta);
      break;
    default:
      panic("Metatables can only be nil or a table");
  }
  return table;
}

static luav lua_getmetatable(luav table) {
  if (lv_gettype(table) != LTABLE) {
    return LUAV_NIL;
  }
  lhash_t *meta = lv_gettable(table)->metatable;
  luav meta_field = meta->metamethods[META_METATABLE];
  if (meta_field != LUAV_NIL) {
    return meta_field;
  }
  return lv_table(meta);
}
