#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "gc.h"
#include "lhash.h"
#include "lib/coroutine.h"
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
static luav lua_next_f;
static luav lua_nexti_f;
static u32  lua_assert(LSTATE);
static u32  lua_type(LSTATE);
static u32  lua_tostring(LSTATE);
static u32  lua_tonumber(LSTATE);
static u32  lua_print(LSTATE);
static u32  lua_select(LSTATE);
static u32  lua_rawget(LSTATE);
static u32  lua_rawset(LSTATE);
static u32  lua_setmetatable(LSTATE);
static u32  lua_getmetatable(LSTATE);
static u32  lua_loadstring(LSTATE);
static u32  lua_pcall(LSTATE);
static u32  lua_xpcall(LSTATE);
static u32  lua_error(LSTATE);
static u32  lua_next(LSTATE);
static u32  lua_pairs(LSTATE);
static u32  lua_nexti(LSTATE);
static u32  lua_ipairs(LSTATE);
static u32  lua_unpack(LSTATE);
static u32  lua_dofile(LSTATE);
static u32  lua_getfenv(LSTATE);
static u32  lua_setfenv(LSTATE);
static u32  lua_rawequal(LSTATE);
static u32  lua_loadfile(LSTATE);
static void lua_base_gc(void);

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

  cfunc_register(lua_globals, "assert",        lua_assert);
  cfunc_register(lua_globals, "type",          lua_type);
  cfunc_register(lua_globals, "tostring",      lua_tostring);
  cfunc_register(lua_globals, "print",         lua_print);
  cfunc_register(lua_globals, "tonumber",      lua_tonumber);
  cfunc_register(lua_globals, "select",        lua_select);
  cfunc_register(lua_globals, "rawget",        lua_rawget);
  cfunc_register(lua_globals, "rawset",        lua_rawset);
  cfunc_register(lua_globals, "setmetatable",  lua_setmetatable);
  cfunc_register(lua_globals, "getmetatable",  lua_getmetatable);
  cfunc_register(lua_globals, "loadstring",    lua_loadstring);
  cfunc_register(lua_globals, "pcall",         lua_pcall);
  cfunc_register(lua_globals, "xpcall",        lua_xpcall);
  cfunc_register(lua_globals, "error",         lua_error);
  cfunc_register(lua_globals, "next",          lua_next);
  cfunc_register(lua_globals, "pairs",         lua_pairs);
  cfunc_register(lua_globals, "ipairs",        lua_ipairs);
  cfunc_register(lua_globals, "unpack",        lua_unpack);
  cfunc_register(lua_globals, "dofile",        lua_dofile);
  cfunc_register(lua_globals, "getfenv",       lua_getfenv);
  cfunc_register(lua_globals, "setfenv",       lua_setfenv);
  cfunc_register(lua_globals, "rawequal",      lua_rawequal);
  cfunc_register(lua_globals, "loadfile",      lua_loadfile);

  lua_next_f = lhash_get(lua_globals, LSTR("next"));
  lua_nexti_f = lv_function(cfunc_alloc(lua_nexti, "nexti", 0));
  gc_add_hook(lua_base_gc);
}

static void lua_base_gc() {
  gc_traverse(lua_next_f);
  gc_traverse(lua_nexti_f);
}

static u32 lua_assert(LSTATE) {
  u8 boolean = lstate_getbool(0);
  if (!boolean) {
    char *explain = "assertion failed!";
    if (argc > 1) {
      explain = lstate_getstring(1)->data;
    }
    err_rawstr(explain, TRUE);
  }
  u32 i;
  for (i = 0; i < argc && i < retc; i++) {
    lstate_return(lstate_getval(i), i);
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
  lstring_t *str = lstr_alloc(LUAV_INIT_STRING);
  int len;

  luav v = lstate_getval(0);
  switch (lv_gettype(v)) {
    case LNIL:
      len = sprintf(str->data, "nil");
      break;
    case LSTRING:
      lstate_return1(v);
    case LFUNCTION:
      len = sprintf(str->data, "function: %p", lv_getfunction(v, 0));
      break;
    case LBOOLEAN:
      len = sprintf(str->data, lv_getbool(v, 0) ? "true" : "false");
      break;
    case LNUMBER: {
      len = snprintf(str->data, LUAV_INIT_STRING, LUA_NUMBER_FMT,
                     lv_castnumber(v, 0));
      break;
    }
    case LTHREAD:
      len = sprintf(str->data, "thread: %p", lv_getthread(v, 0));
      break;

    case LUSERDATA:
    case LTABLE: {
      lhash_t *meta = getmetatable(v);
      if (meta) {
        luav value = lhash_get(meta, META_TOSTRING);
        if (value != LUAV_NIL) {
          vm_stack->base[argvi] = v;
          return vm_fun(lv_getfunction(value, 0), vm_running, 1, argvi,
                        retc, retvi);
        }
      }
      if (lv_gettype(v) == LTABLE) {
        len = sprintf(str->data, "table: %p", lv_gettable(v, 0));
      } else {
        len = sprintf(str->data, "userdata: %p", lv_getuserdata(v, 0));
      }
      break;
    }

    default:
      panic("Unknown luav: 0x%016" PRIu64, v);
  }

  str->length = (size_t) len;
  v = lv_string(lstr_add(str));
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
        printf("%.*s", (int) str->length, str->data);
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
    lstate_return(lstate_getval(i + off), i);
  }
  return i;
}

static u32 lua_rawget(LSTATE) {
  luav value = lhash_get(lstate_gettable(0), lstate_getval(1));
  lstate_return1(value);
}

static u32 lua_rawset(LSTATE) {
  lhash_set(lstate_gettable(0), lstate_getval(1), lstate_getval(2));
  lstate_return1(lstate_getval(0));
}

static u32 lua_setmetatable(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  // make sure the current metatable isn't protected
  lhash_t *old = table->metatable;
  if (old != NULL && lhash_get(old, META_METATABLE) != LUAV_NIL)
    err_rawstr("You cannot replace a protected metatable", TRUE);

  luav value = lstate_getval(1);
  if (value == LUAV_NIL) {
    table->metatable = NULL;
  } else {
    table->metatable = lstate_gettable(1);
  }
  lstate_return1(lv_table(table));
}

static u32 lua_getmetatable(LSTATE) {
  luav value = lstate_getval(0);
  if (!lv_istable(value)) {
    lstate_return1(LUAV_NIL);
  }
  lhash_t *meta = lv_gettable(value, 0)->metatable;
  if (meta == NULL) {
    lstate_return1(LUAV_NIL);
  }
  luav meta_field = lhash_get(meta, META_METATABLE);
  if (meta_field != LUAV_NIL) {
    lstate_return1(meta_field);
  }
  lstate_return1(lv_table(meta));
}

static u32 lua_loadstring(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  lstring_t *name = (argc > 1) ? lstate_getstring(1) : str;

  lfunc_t *func = gc_alloc(sizeof(lfunc_t), LFUNC);
  memset(func, 0, sizeof(lfunc_t));
  if (luac_parse_string(func, str->data, str->length, name->data) < 0) {
    lstate_return(LUAV_NIL, 0);
    lstate_return(LSTR("Bad lua string"), 1);
    return 2;
  }

  lclosure_t *closure   = gc_alloc(sizeof(lclosure_t), LFUNCTION);
  closure->type         = LUAF_LUA;
  closure->function.lua = func;
  closure->env          = global_env;

  lstate_return1(lv_function(closure));
}

static u32 lua_pcall(LSTATE) {
  lclosure_t *closure = lstate_getfunction(0);
  int err;
  u32 ret;
  ONERR({
    ret = vm_fun(closure, vm_running, argc - 1, argvi + 1,
                                      retc - 1, retvi + 1);
    lstate_return(LUAV_TRUE, 0);
  }, {
    lstate_return(LUAV_FALSE, 0);
    lstate_return(err_value, 1);
  }, err);

  return err ? 2 : ret + 1;
}

static u32 lua_xpcall(LSTATE) {
  lclosure_t *f = lstate_getfunction(0);
  lclosure_t *err = lstate_getfunction(1);
  int iserr;
  volatile int tried = 0;
  u32 ret;
  lframe_t *running = vm_running;

  ONERR({
    ret = vm_fun(f, running, 0, 0, retc - 1, retvi + 1);
    lstate_return(LUAV_TRUE, 0);
  }, {
    luav retval;
    /* If the error handling function has an error, we need to return something
       different, otherwise invoke the error handling function */
    if (!tried) {
      tried = 1;
      u32 idx = vm_stack_alloc(vm_stack, 1);
      vm_stack->base[idx] = err_value;
      vm_fun(err, running, 1, idx, 1, idx);
      retval = vm_stack->base[idx];
      vm_stack_dealloc(vm_stack, idx);
    } else {
      retval = LSTR("error in error handling");
    }
    lstate_return(LUAV_FALSE, 0);
    lstate_return(retval, 1);
  }, iserr)

  return iserr ? 2 : ret + 1;
}

static u32 lua_error(LSTATE) {
  luav value = argc > 0 ? lstate_getval(0) : LUAV_NIL;
  u32 level = 1;
  if (argc > 1) {
    level = (u32) lstate_getnumber(1);
  }
  lframe_t *frame = vm_running;
  while (level-- > 0 && frame != NULL) {
    frame = frame->caller;
  }

  err_luav(frame, value);
}

static u32 lua_next(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  luav key = LUAV_NIL;
  if (argc > 1) {
    key = lstate_getval(1);
  }
  luav nxtkey, nxtvalue;
  lhash_next(table, key, &nxtkey, &nxtvalue);
  lstate_return(nxtkey, 0);
  lstate_return(nxtvalue, 1);
  return 2;
}

static u32 lua_pairs(LSTATE) {
  lstate_return(lua_next_f, 0);
  lstate_return(lv_table(lstate_gettable(0)), 1);
  lstate_return(LUAV_NIL, 2);
  return 3;
}

static u32 lua_nexti(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  double num = lstate_getnumber(1) + 1;
  luav value = lhash_get(table, lv_number(num));
  if (value == LUAV_NIL) {
    return 0;
  }
  lstate_return(lv_number(num), 0);
  lstate_return(value, 1);
  return 2;
}

static u32 lua_ipairs(LSTATE) {
  lstate_return(lua_nexti_f, 0);
  lstate_return(lv_table(lstate_gettable(0)), 1);
  lstate_return(lv_number(0), 2);
  return 3;
}

static u32 lua_unpack(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  u32 i = argc > 1 ? (u32) lstate_getnumber(1) : 1;
  u32 j = argc > 2 ? (u32) lstate_getnumber(2) : (u32) table->length;

  u32 k;
  if (i > j) { return 0; }
  for (k = 0; k < retc && k <= j - i; k++) {
    lstate_return(lhash_get(table, lv_number(k + i)), k);
  }
  return k;
}

static u32 lua_dofile(LSTATE) {
  if (argc == 0) {
    panic("Read from stdin? That's silly.");
  }
  lstring_t *filename = lstate_getstring(0);
  lfunc_t func;
  if (luac_parse_file(&func, filename->data) < 0) {
    err_rawstr("Bad lua file.", 0);
  }
  lclosure_t *closure = gc_alloc(sizeof(lclosure_t), LFUNCTION);
  closure->function.lua = &func;
  closure->type         = LUAF_LUA;
  closure->env          = vm_running->closure->env;
  return vm_fun(closure, vm_running, 0, 0, retc, retvi);
}

static u32 lua_getfenv(LSTATE) {
  luav f = lv_number(1);
  if (argc > 0) {
    f = lstate_getval(0);
  }

  if (lv_isfunction(f)) {
    lclosure_t *closure = lv_getfunction(f, 0);
    lstate_return1(lv_table(closure->env));
  } else {
    u32 lvl = f == LUAV_NIL ? 1 : (u32) lv_castnumber(f, 0);

    if (lvl == 0) { lstate_return1(lv_table(global_env)); }
    lframe_t *cur = vm_running;
    while (lvl-- > 0) {
      if (cur->caller == NULL) {
        err_str(0, "invalid level");
      }
      cur = cur->caller;
    }
    lstate_return1(lv_table(cur->closure->env));
  }
}

static u32 lua_setfenv(LSTATE) {
  luav f = lv_number(1);
  if (argc > 0) {
    f = lstate_getval(0);
  }
  lhash_t *table = lstate_gettable(1);

  if (lv_isfunction(f)) {
    lclosure_t *closure = lv_getfunction(f, 0);
    closure->env = table;
  } else {
    u32 lvl = (u32) lv_castnumber(f, 0);

    if (lvl == 0) {
      global_env = table;
      return 0;
    }
    lframe_t *cur = vm_running;
    while (lvl-- > 0) {
      if (cur->caller == NULL) {
        err_str(0, "invalid level");
      }
      cur = cur->caller;
    }
    cur->closure->env = table;
    f = lv_function(cur->closure);
  }

  lstate_return1(f);
}

static u32 lua_rawequal(LSTATE) {
  lstate_return1(lv_bool(lstate_getval(0) == lstate_getval(1)));
}

/**
 * @brief Read a file (or stdin), and return a function to execute it
 */
static u32 lua_loadfile(LSTATE) {
  lstring_t *fname = lstate_getstring(0);

  lfunc_t *func = gc_alloc(sizeof(lfunc_t), LFUNC);
  if (luac_parse_file(func, fname->data) < 0) {
    lstate_return(LUAV_NIL, 0);
    lstate_return(LSTR("Bad file"), 1);
    return 2;
  }

  lclosure_t *closure = gc_alloc(sizeof(lclosure_t), LFUNCTION);
  closure->type = LUAF_LUA;
  closure->function.lua = func;
  closure->env = global_env;

  lstate_return1(lv_function(closure));
}
