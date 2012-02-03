#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

#include "lstate.h"
#include "lstring.h"
#include "luav.h"

#define VM_STACK_INIT 1024

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

  // debug information
  u32           num_lines;
  u32           *lines;
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
  u32 **pc;
  struct lframe *caller;
} lframe_t;

typedef struct lstack {
  luav *top;
  luav *base;
  u32 size;
  u32 limit;
} lstack_t;

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
extern lstack_t vm_stack;
extern struct lhash *global_env;

void vm_run(lfunc_t *fun);
u32 vm_fun(lclosure_t *c, lframe_t *frame, LSTATE);
void vm_stack_init(lstack_t *stack, u32 size);

void vm_stack_grow(lstack_t *stack, u32 size);
u32 vm_stack_alloc(lstack_t *stack, u32 size);
void vm_stack_dealloc(lstack_t *stack, u32 base);

#endif /* _VM_H_ */
