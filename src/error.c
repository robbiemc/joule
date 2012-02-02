#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "panic.h"
#include "util.h"
#include "vm.h"

#define ERR_MISSING  1
#define ERR_BADTYPE  2
#define ERR_STR      3
#define ERR_RAWSTR   4
#define ERR_NOPOSSTR 5

char *lua_program = NULL;
jmp_buf *err_catcher = NULL;
char err_desc[ERRBUF_SIZE];

static u32 err_info[10];
static char *err_custom;

char* err_typestr(u32 type) {
  switch (type) {
    case LNIL:      return "nil";
    case LTHREAD:   return "thread";
    case LNUMBER:   return "number";
    case LFUNCTION: return "function";
    case LUSERDATA: return "userdata";
    case LTABLE:    return "table";
    case LANY:      return "value";
    case LSTRING:   return "string";
    case LBOOLEAN:  return "boolean";
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
  lclosure_t *caller;
  lfunc_t *func;
  lframe_t *caller_frame;
  if (frame->caller == NULL || frame->closure->type == LUAF_LUA) {
    caller = frame->closure;
    xassert(caller->type == LUAF_LUA);
    func = caller->function.lua;
    caller_frame = frame;
  } else {
    caller = frame->caller->closure;
    xassert(caller != NULL && caller->type == LUAF_LUA);
    func = caller->function.lua;
    caller_frame = frame->caller;
  }

  int len;

  /* Figure out debug information from the luac file of where the call came
     from (source line) */
  xassert(frame->pc < func->dbg_linecount);
  len = sprintf(err_desc, "%.25s:%u: ", func->file,
                func->dbg_lines[caller_frame->pc - 1]);

  switch (err) {
    case ERR_MISSING:
      len += sprintf(err_desc + len,
                     "bad argument #%d to '%s' (%s expected, got no value)",
                     err_info[0] + 1, funstr(vm_running->closure),
                     err_typestr(err_info[1]));
      break;

    case ERR_BADTYPE:
      len += sprintf(err_desc + len,
                     "bad argument #%d to '%s' (%s expected, got %s)",
                     err_info[0] + 1, funstr(vm_running->closure),
                     err_typestr(err_info[1]), err_typestr(err_info[2]));
      break;

    case ERR_STR:
      len += sprintf(err_desc + len,
                     "bad argument #%d to '%s' (%s)",
                     err_info[0] + 1, funstr(vm_running->closure),
                     err_custom);
      break;

    case ERR_RAWSTR:
      len += sprintf(err_desc + len, "%s", err_custom);
      break;

    case ERR_NOPOSSTR:
      len = sprintf(err_desc, "%s", err_custom);
      break;

    default:
      panic("Unknown error type: %d", err);
  }

  if (err_catcher != NULL) {
    longjmp(*err_catcher, 1);
  }

  xassert(lua_program != NULL);
  printf("%s: %s\n", lua_program, err_desc);

  printf("stack traceback:\n");

  while (frame != NULL) {
    lclosure_t *closure = frame->closure;
    printf("\t");
    if (closure->type == LUAF_C) {
      printf("[C]: in function '%s'", closure->function.c->name);
    } else {
      lfunc_t *function = closure->function.lua;
      xassert(frame->pc - 1 < function->dbg_linecount);
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

void err_noposstr(char *explain) {
  err_custom = explain;
  err_explain(ERR_NOPOSSTR, vm_running);
}

void err_fromframe(lframe_t *frame, char *explain) {
  err_custom = explain;
  err_explain(ERR_NOPOSSTR, frame);
}
