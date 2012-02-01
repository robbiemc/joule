#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "lhash.h"
#include "lstate.h"
#include "luav.h"
#include "meta.h"
#include "panic.h"
#include "parse.h"
#include "vm.h"

static luav str_number;
static luav str_nil;
static luav str_boolean;
static luav str_string;
static luav str_table;
static luav str_function;
static luav str_userdata;
static luav str_thread;
static luav str_hash;
static luav str_metatable;
static u32  lua_assert(LSTATE);
static u32  lua_type(LSTATE);
static u32  lua_tostring(LSTATE);
static u32  lua_tonumber(LSTATE);
static u32  lua_print(LSTATE);
static u32  lua_select(LSTATE);
static u32  lua_rawget(LSTATE);
static u32  lua_setmetatable(LSTATE);
static u32  lua_getmetatable(LSTATE);
static u32  lua_loadstring(LSTATE);
static u32  lua_pcall(LSTATE);
static u32  lua_xpcall(LSTATE);

static LUAF(lua_assert);
static LUAF(lua_type);
static LUAF(lua_tostring);
static LUAF(lua_print);
static LUAF(lua_select);
static LUAF(lua_tonumber);
static LUAF(lua_rawget);
static LUAF(lua_setmetatable);
static LUAF(lua_getmetatable);
static LUAF(lua_loadstring);
static LUAF(lua_pcall);
static LUAF(lua_xpcall);

INIT static void lua_utils_init() {
  str_number    = LSTR("number");
  str_nil       = LSTR("nil");
  str_boolean   = LSTR("boolean");
  str_string    = LSTR("string");
  str_table     = LSTR("table");
  str_function  = LSTR("function");
  str_userdata  = LSTR("userdata");
  str_thread    = LSTR("thread");
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
  REGISTER(&lua_globals, "loadstring",    &lua_loadstring_f);
  REGISTER(&lua_globals, "pcall",         &lua_pcall_f);
  REGISTER(&lua_globals, "xpcall",        &lua_xpcall_f);
}

static u32 lua_assert(LSTATE) {
  u8 boolean = lstate_getbool(0);
  if (!boolean) {
    char *explain = "assertion failed!";
    if (argc > 1) {
      explain = lstate_getstring(1)->ptr;
    }
    err_rawstr(explain);
  }
  u32 i;
  for (i = 0; i < argc && i < retc; i++) {
    retv[i] = argv[i];
  }
  return i;
}

static u32 lua_type(LSTATE) {
  luav v = lstate_getval(0);
  switch (lv_gettype(v)) {
    case LNUMBER:   lstate_return1(str_number);
    case LNIL:      lstate_return1(str_nil);
    case LBOOLEAN:  lstate_return1(str_boolean);
    case LSTRING:   lstate_return1(str_string);
    case LTABLE:    lstate_return1(str_table);
    case LFUNCTION: lstate_return1(str_function);
    case LUSERDATA: lstate_return1(str_userdata);
    case LTHREAD:   lstate_return1(str_thread);
  }

  panic("Unknown luav: 0x%016" PRIu64, v);
}

static u32 lua_tostring(LSTATE) {
  /* TODO: if v has a metatable, call __tostring method */
  char *strbuf = xmalloc(LUAV_INIT_STRING);
  int len;

  luav v = lstate_getval(0);
  switch (lv_gettype(v)) {
    case LNIL:
      len = sprintf(strbuf, "nil");
      break;
    case LSTRING:
      lstate_return1(v);
    case LTABLE:
      len = sprintf(strbuf, "table: %p", lv_gettable(v, 0));
      break;
    case LFUNCTION:
      len = sprintf(strbuf, "function: %p", lv_getfunction(v, 0));
      break;
    case LUSERDATA:
      len = sprintf(strbuf, "userdata: %p", lv_getuserdata(v, 0));
      break;
    case LBOOLEAN:
      len = sprintf(strbuf, lv_getbool(v, 0) ? "true" : "false");
      break;
    case LNUMBER: {
      len = snprintf(strbuf, LUAV_INIT_STRING, LUA_NUMBER_FMT,
                     lv_castnumber(v, 0));
      break;
    }
    case LTHREAD:
      len = sprintf(strbuf, "thread: %p", lv_getthread(v, 0));
      break;

    default:
      panic("Unknown luav: 0x%016" PRIu64, v);
  }

  v = lv_string(lstr_add(strbuf, (size_t) len, TRUE));
  lstate_return1(v);
}

static u32 lua_print(LSTATE) {
  u32 i;
  luav value;
  for (i = 0; i < argc; i++) {
    value = lstate_getval(i);
    switch (lv_gettype(value)) {
      case LNIL:      printf("nil");                                    break;
      case LTABLE:    printf("table: %p", lv_gettable(value, 0));       break;
      case LFUNCTION: printf("function: %p", lv_getfunction(value, 0)); break;
      case LUSERDATA: printf("userdata: %p", lv_getuserdata(value, 0)); break;
      case LTHREAD:   printf("thread: %p", lv_getthread(value, 0));     break;
      case LNUMBER:   printf(LUA_NUMBER_FMT, lv_castnumber(value, 0));  break;
      case LBOOLEAN:  printf(lv_getbool(value, 0) ? "true" : "false");  break;
      case LSTRING: {
        lstring_t *str = lv_caststring(value, i);
        printf("%.*s", (int) str->length, str->ptr);
        break;
      }

      default:
        panic("Unknown luav: 0x%016" PRIu64, value);
    }
    if (i < argc - 1) {
      printf("\t");
    }
  }
  printf("\n");
  return 0;
}

static u32 lua_tonumber(LSTATE) {
  u32 base = 10;
  if (argc > 1) {
    base = (u32) lstate_getnumber(1);
  }
  luav value = lstate_getval(0);
  switch (lv_gettype(value)) {
    case LTABLE:
    case LFUNCTION:
    case LUSERDATA:
    case LTHREAD:
    case LBOOLEAN:
    case LNIL:      lstate_return1(LUAV_NIL);
    case LNUMBER:   lstate_return1(value);
    case LSTRING: {
      double ret;
      lstring_t *str = lv_caststring(value, 0);
      if (lv_parsenum(str, base, &ret) < 0) {
        lstate_return1(LUAV_NIL);
      } else {
        lstate_return1(lv_number(ret));
      }
    }

    default:
      panic("Unknown luav: 0x%016" PRIu64, value);
  }

  double num = lv_castnumberb(lstate_getval(0), base, 0);
  lstate_return1(lv_number(num));
}

static u32 lua_select(LSTATE) {
  luav first = lstate_getval(0);
  if (first == str_hash) {
    lstate_return1(lv_number(argc - 1));
  }

  u32 i, off = (u32) lv_castnumber(first, 0);
  for (i = 0; i < retc && i + off < argc; i++) {
    retv[i] = argv[i + off];
  }
  return i;
}

static u32 lua_rawget(LSTATE) {
  luav value = lhash_get(lstate_gettable(0), lstate_getval(1));
  lstate_return1(value);
}

static u32 lua_setmetatable(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  // make sure the current metatable isn't protected
#ifndef NDEBUG
  lhash_t *old = table->metatable;
  assert(old == NULL || old->metamethods[META_METATABLE] == LUAV_NIL);
#endif

  luav value = lstate_getval(1);
  switch (lv_gettype(value)) {
    case LNIL:
      table->metatable = NULL;
      break;
    case LTABLE:
      table->metatable = lv_gettable(value, 1);
      break;
    default:
      panic("Metatables can only be nil or a table");
  }
  lstate_return1(lv_table(table));
}

static u32 lua_getmetatable(LSTATE) {
  luav value = lstate_getval(0);
  if (!lv_istable(value)) {
    lstate_return1(LUAV_NIL);
  }
  lhash_t *meta = lv_gettable(value, 0)->metatable;
  luav meta_field = meta->metamethods[META_METATABLE];
  if (meta_field != LUAV_NIL) {
    lstate_return1(meta_field);
  }
  lstate_return1(lv_table(meta));
}

static u32 lua_loadstring(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  lstring_t *name = (argc > 1) ? lstate_getstring(1) : str;

  luac_file_t *file = xmalloc(sizeof(luac_file_t));
  luac_parse_string(file, str->ptr, str->length, name->ptr);

  lclosure_t *closure = xmalloc(sizeof(lclosure_t));
  closure->type = LUAF_LUA;
  closure->function.lua = &file->func;

  lstate_return1(lv_function(closure));
}

static u32 lua_pcall(LSTATE) {
  lclosure_t *closure = lstate_getfunction(0);
  jmp_buf onerr;
  jmp_buf *prev = err_catcher;
  err_catcher = &onerr;
  if (setjmp(onerr) != 0) {
    lstate_return(LUAV_FALSE, 0);
    lstate_return(lv_string(lstr_add(err_desc, strlen(err_desc), FALSE)), 1);
    err_catcher = prev;
    return 2;
  }

  u32 ret = vm_fun(closure, vm_running, argc - 1, argv + 1, retc - 1, retv + 1);
  err_catcher = prev;
  lstate_return(LUAV_TRUE, 0);
  return ret + 1;
}

static u32 lua_xpcall(LSTATE) {
  lclosure_t *f = lstate_getfunction(0);
  lclosure_t *err = lstate_getfunction(1);
  jmp_buf onerr;
  jmp_buf *prev = err_catcher;
  err_catcher = &onerr;
  int tried = 0;

  if (setjmp(onerr) != 0) {
    luav retval;
    if (!tried) {
      tried = 1;
      luav error = lv_string(lstr_add(err_desc, strlen(err_desc), FALSE));
      retval = LUAV_NIL;
      vm_fun(err, vm_running, 1, &error, 1, &retval);
    } else {
      retval = LSTR("error in error handling");
    }
    lstate_return(LUAV_FALSE, 0);
    lstate_return(retval, 1);
    err_catcher = prev;
    return 2;
  }

  u32 ret = vm_fun(f, vm_running, 0, NULL, retc - 1, retv + 1);
  err_catcher = prev;
  lstate_return(LUAV_TRUE, 0);
  return ret + 1;
}
