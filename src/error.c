#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "panic.h"
#include "util.h"
#include "vm.h"

#define ERR_MISSING 1
#define ERR_BADTYPE 2
#define ERR_STR     3
#define ERR_RAWSTR  4

char *lua_program = NULL;

static u32 err_info[10];
static char *err_custom;

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

void err_explain(int err, lframe_t *frame) {
  assert(frame->caller != NULL);
  lclosure_t *caller = frame->caller->closure;
  assert(caller != NULL && caller->type == LUAF_LUA);
  lfunc_t *func = caller->function.lua;

  /* Figure out debug information from the luac file of where the call came
     from (source line) */
  assert(frame->pc < func->dbg_linecount);
  assert(lua_program != NULL);
  printf("%s: %s:%u: ", lua_program, func->file, func->dbg_lines[frame->pc]);

  switch (err) {
    case ERR_MISSING:
      printf("bad argument #%d to '%s' (%s expected, got no value)\n",
             err_info[0] + 1, funstr(vm_running->closure),
             typestr(err_info[1]));
      break;

    case ERR_BADTYPE:
      printf("bad argument #%d to '%s' (%s expected, got %s)\n",
             err_info[0] + 1, funstr(vm_running->closure), typestr(err_info[1]),
             typestr(err_info[2]));
      break;

    case ERR_STR:
      printf("bad argument #%d to '%s' (%s)\n",
             err_info[0] + 1, funstr(vm_running->closure), err_custom);
      break;

    case ERR_RAWSTR:
      printf("%s\n", err_custom);
      break;

    default:
      panic("Unknown error type: %d", err);
  }

  printf("stack traceback:\n");

  while (frame != NULL) {
    lclosure_t *closure = frame->closure;
    printf("\t");
    if (closure->type == LUAF_C) {
      printf("[C]: in function '%s'", closure->function.c->name);
    } else {
      lfunc_t *function = closure->function.lua;
      assert(frame->pc - 1 < function->dbg_linecount);
      printf("%s:%d: ", function->file, function->dbg_lines[frame->pc - 1]);
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
    frame = frame->caller;
  }

  exit(1);
}

void err_missing(u32 n, u32 expected_type) {
  err_info[0] = n;
  err_info[1] = expected_type;
  err_explain(ERR_MISSING, vm_running);
}

void err_badtype(u32 n, u32 expected, u32 got) {
  err_info[0] = n;
  err_info[1] = expected;
  err_info[2] = got;
  err_explain(ERR_BADTYPE, vm_running);
}

void err_str(u32 n, char *explain) {
  err_info[0] = n;
  err_custom = explain;
  err_explain(ERR_STR, vm_running);
}

void err_rawstr(char *explain) {
  err_custom = explain;
  err_explain(ERR_RAWSTR, vm_running);
}
