#include <stdarg.h>
#include <stdio.h>

#include "config.h"
#include "debug.h"
#include "luav.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

#define __pr(...) prindent(level, out, __VA_ARGS__)
#define pr(...) fprintf(out, __VA_ARGS__)
#define iloop(len) width = log_10(func->len); \
                           for (i = 0; i < func->len; i++)

static void dbg_dump_func(FILE *out, lfunc_t *func, int level);
static void dbg_dump_lstring(FILE *out, lstring_t *str);
static void prindent(int level, FILE *out, char *fmt, ...);
static int log_10(u64 n);

void dbg_dump_function(FILE *out, lfunc_t *func) {
  dbg_dump_func(out, func, 0);
}

static void dbg_dump_func(FILE *out, lfunc_t *func, int level) {
  size_t i;
  int width;

  __pr("function ");
  //dbg_dump_lstring(out, lstr_get(func->name));
  pr(": Lines %d - %d\n", func->start_line, func->end_line);

  __pr("  %d parameters, ", func->num_parameters);
  switch (func->is_vararg) {
    case 1: pr("VA_HASARG\n");    break;
    case 2: pr("VA_ISVARARG\n");  break;
    case 4: pr("VA_NEEDSARG\n");  break;
    default: pr("<! unrecognized is_vararg flag !>\n");
  }
  if (func->compilable) {
    __pr("  compilable\n");
  }

  __pr("  %d upvalues, %d max stack size\n", func->num_upvalues,
                                              func->max_stack);

  __pr("  Constants: %zu\n", func->num_consts);
  iloop(num_consts) {
    __pr("  %*zu   ", width, i);
    dbg_dump_luav(out, func->consts[i]);
    pr("\n");
  }

  __pr("  Instructions: %zu\n", func->num_instrs);
  iloop(num_instrs) {
    __pr("  %*zu   ", width, i);
    opcode_dump(out, func->instrs[i].instr);
    pr("\n");
  }

  __pr("  Nested Functions: %zu\n", func->num_funcs);
  iloop(num_funcs) {
    dbg_dump_func(out, func->funcs[i], level+1);
  }
}

void dbg_dump_luav(FILE *out, luav value) {
  switch (lv_gettype(value)) {
    case LNUMBER:   pr(LUA_NUMBER_FMT, lv_castnumber(value, 0));       return;
    case LTABLE:    pr("Table: %p", lv_gettable(value, 0));            return;
    case LBOOLEAN:  pr("%s", lv_getbool(value, 0) ? "true" : "false"); return;
    case LNIL:      pr("nil");                                         return;
    case LFUNCTION: pr("Function: %p", lv_getfunction(value, 0));      return;
    case LTHREAD:   pr("Thread: %p", lv_getthread(value, 0));          return;
    case LUSERDATA: pr("Userdata: %p", lv_getuserdata(value, 0));      return;
    case LSTRING:
      pr("\"");
      dbg_dump_lstring(out, lv_caststring(value, 0));
      pr("\"");
      return;
  }
  panic("lv_gettype seems to be broken... %d", lv_gettype(value));
}

static void dbg_dump_lstring(FILE *out, lstring_t *str) {
  /*
   * The default lua interpreter conveniently stops printing strings
   * when it encounters a null character, so so will we. This means
   * we can use built-in printing functions!
   *
   * Unfortunately, '.*' in a format string requires a parameter of
   * type int, not size_t as it should, so you're limited to printing
   * the first 2-billion characters of a string. I think the safety
   * in this case outweighs the limitation.
   */
  fprintf(out, "%.*s", (int) str->length, str->data);
}

static void prindent(int level, FILE *out, char *fmt, ...) {
  // print the indentation
  int i;
  for (i = 0; i < level; i++)
    fprintf(out, "    ");
  // print the string
  va_list args;
  va_start(args, fmt);
  vfprintf(out, fmt, args);
  va_end(args);
}

static int log_10(u64 n) {
  return 3;
  /*
  int log = 0;
  while (n > 0) {
    n /= 10;
    log++;
  }
  if (log == 0)
    return 1;
  return log;
  */
}
