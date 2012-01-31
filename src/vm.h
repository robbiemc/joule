#ifndef _VM_H_
#define _VM_H_

#include <setjmp.h>
#include <stdint.h>

#include "lstring.h"
#include "luav.h"
#include "lstate.h"

struct lhash;

typedef u32 cfunction_t(LSTATE);

typedef struct lfunc {
  lstr_idx      name;
  int           start_line;
  int           end_line;
  uint8_t       num_upvalues;
  uint8_t       num_parameters;
  uint8_t       is_vararg;
  uint8_t       max_stack;

  size_t        num_instrs;
  uint32_t      *instrs;
  size_t        num_consts;
  luav          *consts;
  size_t        num_funcs;
  struct lfunc  *funcs;

  // raw pointers to debug information - no parsing is done for these
  void          *dbg_lines;
  void          *dbg_locals;
  void          *dbg_upvalues;
} lfunc_t;

typedef struct lclosure {
  u32 type;
  union {
    lfunc_t *lua;
    cfunction_t *c;
  } function;
  luav upvalues[0];
} lclosure_t;

#define LUAF_NIL  0
#define LUAF_C    1
#define LUAF_LUA  2
#define CLOSURE_SIZE(num_upvalues) \
  (sizeof(lclosure_t) + (num_upvalues) * sizeof(luav))

#define LUAF(fun) \
  lclosure_t fun ## _f = {.type = LUAF_C, .function.c = fun}

#define REGISTER(tbl, str, fun) lhash_set(tbl, LSTR(str), lv_function(fun))

extern struct lhash lua_globals;
extern jmp_buf *vm_jmpbuf;
extern lclosure_t *vm_running;

void vm_run(lfunc_t *fun);
u32 vm_fun(lclosure_t *c, u32 argc, luav *argv, u32 retc, luav *retv);

#endif /* _VM_H_ */
