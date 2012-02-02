#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

#include "lstring.h"
#include "luav.h"
#include "lstate.h"

struct lhash;

typedef u32 cfunction_t(LSTATE);

typedef struct lfunc {
  lstr_idx      name;
  char          *file;
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
  u32           dbg_linecount;
  u32           *dbg_lines;
  void          *dbg_locals;
  void          *dbg_upvalues;
} lfunc_t;

typedef struct cfunc {
  cfunction_t *f;
  char *name;
} cfunc_t;

typedef struct lclosure {
  u32 type;
  struct lhash *env;
  union {
    lfunc_t *lua;
    cfunc_t *c;
  } function;
  luav upvalues[1]; /* TODO: why does this need to be 1? */
} lclosure_t;

typedef struct lframe {
  lclosure_t *closure;
  u32 pc;
  struct lframe *caller;
} lframe_t;

#define LUAF_NIL  0
#define LUAF_C    1
#define LUAF_LUA  2
#define CLOSURE_SIZE(num_upvalues) \
  (sizeof(lclosure_t) + (num_upvalues) * sizeof(luav))

#define LUAF(fun) \
  cfunc_t fun ## _cf = {.f = fun}; \
  lclosure_t fun ## _f = {.type = LUAF_C, .function.c = &fun##_cf, .env = NULL}

#define REGISTER(tbl, str, fun) {                 \
    lhash_set(tbl, LSTR(str), lv_function(fun));  \
    (fun)->function.c->name = str;                \
  }

extern struct lhash lua_globals;
extern lframe_t *vm_running;
extern struct lhash *global_env;

void vm_run(lfunc_t *fun);
u32 vm_fun(lclosure_t *c, lframe_t *frame,
           u32 argc, luav *argv, u32 retc, luav *retv);

#endif /* _VM_H_ */
