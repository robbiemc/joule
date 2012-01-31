#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "panic.h"
#include "util.h"
#include "vm.h"

#define ERR_MISSING 1
#define ERR_BADTYPE 2

char *lua_program = NULL;

static u32 err_info[10];

static char* typestr(u32 type) {
  switch (type) {
    case LNIL:      return "nil";
    case LTHREAD:   return "thread";
    case LNUMBER:   return "number";
    case LFUNCTION: return "function";
    case LUSERDATA: return "userdata";
    case LTABLE:    return "table";
    case LANY:      return "value";
    case LSTRING:   return "string";
  }
  panic("bad type: %d", type);
}

static char* funstr(lclosure_t *closure) {
  if (closure->type == LUAF_LUA) {
    return lstr_get(closure->function.lua->name)->ptr;
  }
  return closure->function.c->name;
}

void err_explain(int err, lclosure_t *closure) {
  lclosure_t *caller = closure->caller;
  assert(caller != NULL && caller->type == LUAF_LUA);
  lfunc_t *func = caller->function.lua;

  /* Figure out debug information from the luac file of where the call came
     from (source line) */
  assert(closure->pc < func->dbg_linecount);
  assert(lua_program != NULL);
  printf("%s: %s:%u: ", lua_program, func->file, func->dbg_lines[closure->pc]);

  switch (err) {
    case ERR_MISSING:
      printf("bad argument #%d to '%s' (%s expected, got no value)\n",
             err_info[0] + 1, funstr(vm_running), typestr(err_info[1]));
      break;

    case ERR_BADTYPE:
      break;

    default:
      panic("Unknown error type: %d", err);
  }

  printf("stack traceback:\n");

  while (closure != NULL) {
    printf("\t");
    if (closure->type == LUAF_C) {
      printf("[C]: in function '%s'", closure->function.c->name);
    } else {
      lfunc_t *function = closure->function.lua;
      assert(closure->pc - 1 < function->dbg_linecount);
      printf("%s:%d: ", function->file, function->dbg_lines[closure->pc - 1]);
      lstring_t *fname = lstr_get(function->name);

      if (fname->length == 0) {
        printf("in function <%s:%d>", function->file, function->start_line);
      } else if (fname->ptr[0] == '@') {
        printf("in main chunk");
      } else {
        printf("in function '%.*s'", (int) fname->length, fname->ptr);
      }
    }
    printf("\n");
    closure = closure->caller;
  }

  exit(1);
}

void err_missing(u32 n, u32 expected_type) {
  err_info[0] = n;
  err_info[1] = expected_type;
  longjmp(*vm_jmpbuf, ERR_MISSING);
}

void err_badtype(u32 n, u32 type) {
  err_info[0] = n;
  err_info[1] = type;
  longjmp(*vm_jmpbuf, ERR_BADTYPE);
}
