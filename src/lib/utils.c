#include <stdio.h>
#include <stdlib.h>

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

static LUAF_1ARG(lua_type);

INIT static void init_utils() {
  str_number   = LSTR("number");
  str_nil      = LSTR("nil");
  str_boolean  = LSTR("boolean");
  str_string   = LSTR("string");
  str_table    = LSTR("table");
  str_function = LSTR("function");
  str_userdata = LSTR("userdata");

  lhash_set(&lua_globals, LSTR("type"), lv_function(&lua_type_f));
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

  printf("Unknown luav: 0x%016llx", v);
  abort();
}
