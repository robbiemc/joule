#include <assert.h>
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
#define ERR_LUAV     5

char *lua_program = NULL;
jmp_buf *err_catcher = NULL;
luav err_value;

static char err_desc[ERRBUF_SIZE];
static u32 err_info[10];
static char *err_custom;

#define GETPC(frame, func) \
  ((u32) ((size_t) *(frame)->pc - (size_t) func->instrs) / sizeof(instr_t))

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
    return closure->function.lua->name->data;
  }
  return closure->function.c->name;
}

void err_explain(int err, lframe_t *frame) {
  lclosure_t *caller;
  lfunc_t *func;
  lframe_t *caller_frame = frame;
  while (caller_frame->closure->type != LUAF_LUA) {
    assert(caller_frame->caller != NULL);
    assert(caller_frame->caller != caller_frame);
    caller_frame = caller_frame->caller;
  }
  caller = caller_frame->closure;
  func   = caller->function.lua;

  int len = 0;
  u32 pc = GETPC(caller_frame, func) - 1;

  /* Figure out debug information from the luac file of where the call came
     from (source line) */
  assert(pc < func->num_lines);

  switch (err) {
    case ERR_MISSING:
      len = sprintf(err_desc,
                    "%.25s:%u: bad argument #%d to '%s' (%s expected, "
                    "got no value)",
                    func->file, func->lines[pc],
                    err_info[0] + 1, funstr(vm_running->closure),
                    err_typestr(err_info[1]));
      break;

    case ERR_BADTYPE:
      len = sprintf(err_desc,
                    "%.25s:%u: bad argument #%d to '%s' (%s expected, got %s)",
                    func->file, func->lines[pc],
                    err_info[0] + 1, funstr(vm_running->closure),
                    err_typestr(err_info[1]), err_typestr(err_info[2]));
      break;

    case ERR_STR:
      len = sprintf(err_desc, "%.25s:%u: bad argument #%d to '%s' (%s)",
                    func->file, func->lines[pc],
                    err_info[0] + 1, funstr(vm_running->closure),
                    err_custom);
      break;

    case ERR_RAWSTR:
      if (err_info[0]) {
        len = sprintf(err_desc, "%.25s:%u: %s", func->file, func->lines[pc],
                      err_custom);
      } else {
        len = sprintf(err_desc, "%s", err_custom);
      }
      break;

    case ERR_LUAV:
      if (err_catcher) {
        if (lv_isstring(err_value) && frame->closure->type == LUAF_LUA) {
          len = sprintf(err_desc, "%.25s:%u: %s", func->file, func->lines[pc],
                        lv_caststring(err_value, 0)->data);
          err_desc[len] = 0;
          err_value = lv_string(lstr_literal(err_desc, FALSE));
        }
      } else {
        len = sprintf(err_desc, "(error object is not a string)");
      }
      break;

    default:
      panic("Unknown error type: %d", err);
  }

  if (err_catcher != NULL) {
    if (err != ERR_LUAV) {
      err_desc[len] = 0;
      err_value = lv_string(lstr_literal(err_desc, FALSE));
    }
    _longjmp(*err_catcher, 1);
  }

  assert(lua_program != NULL);
  printf("%s: %s\n", lua_program, err_desc);

  printf("stack traceback:\n");

  while (frame != NULL) {
    lclosure_t *closure = frame->closure;
    printf("\t");
    if (closure->type == LUAF_C) {
      printf("[C]: in function '%s'", closure->function.c->name);
    } else {
      lfunc_t *function = closure->function.lua;
      pc = GETPC(frame, function) - 1;
      assert(pc < function->num_lines);
      printf("%s:%d: ", function->file, function->lines[pc]);
      lstring_t *fname = function->name;

      if (fname->length == 0) {
        printf("in function <%s:%d>", function->file, function->start_line);
      } else if (fname->data[0] == '@') {
        printf("in main chunk");
      } else {
        printf("in function '%.*s'", (int) fname->length, fname->data);
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

void err_rawstr(char *explain, int withpos) {
  err_custom = explain;
  err_info[0] = (u32) withpos;
  err_explain(ERR_RAWSTR, vm_running);
}

void err_luav(lframe_t *frame, luav value) {
  err_value = value;
  err_explain(ERR_LUAV, frame);
}
