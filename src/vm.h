#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

#include "llvm.h"
#include "lstate.h"
#include "lstring.h"
#include "luav.h"

#define VM_STACK_INIT 1024

struct lhash;

typedef u32 cfunction_t(LSTATE);

typedef struct instr {
  u32       instr;
  u32       count;
  jfunc_t   *jfunc;
} instr_t;

typedef struct lfunc {
  lstring_t   *name;
  char        *file;
  u32         start_line;
  u32         end_line;
  u8          num_upvalues;
  u8          num_parameters;
  u8          is_vararg;
  u8          max_stack;

  size_t      num_instrs;
  instr_t     *instrs;
  size_t      num_consts;
  luav        *consts;
  size_t      num_funcs;
  struct lfunc **funcs;

  // debug information
  u32           num_lines;
  u32           *lines;
} lfunc_t;

typedef struct cfunc {
  cfunction_t *f;
  char *name;
  int upvalues;
} cfunc_t;

typedef struct lclosure {
  u32 type;
  struct lhash *env;
  union {
    lfunc_t *lua;
    cfunc_t *c;
  } function;
  luav upvalues[1];
} lclosure_t;

typedef struct lframe {
  lclosure_t *closure;
  instr_t **pc;
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
#define CLOSURE_SIZE(upvalues) (sizeof(lclosure_t) + (upvalues) * sizeof(luav))

lclosure_t* cfunc_alloc(cfunction_t *f, char *name, int upvalues);
void cfunc_register(struct lhash *table, char *name, cfunction_t *f);

extern struct lhash *lua_globals;
extern lframe_t *vm_running;
extern lstack_t *vm_stack;
extern struct lhash *global_env;

void vm_run(lfunc_t *fun);
u32 vm_fun(lclosure_t *c, lframe_t *frame, LSTATE);
void vm_stack_init(lstack_t *stack, u32 size);
void vm_stack_destroy(lstack_t *stack);

void vm_stack_grow(lstack_t *stack, u32 size);
u32 vm_stack_alloc(lstack_t *stack, u32 size);
void vm_stack_dealloc(lstack_t *stack, u32 base);

#endif /* _VM_H_ */
