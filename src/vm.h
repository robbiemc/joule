#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>
#include "lstring.h"
#include "luav.h"

typedef u32 cfunc_t(u32, luav*, u32, luav*);

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
    cfunc_t *c;
  } function;
  luav upvalues[0];
} lclosure_t;

#define LUAF_CFUNCTION 0
#define LUAF_LFUNCTION 1
#define CLOSURE_SIZE(num_upvalues) \
  (sizeof(lclosure_t) + (num_upvalues) * sizeof(luav))

#define LUA_FUNCTION(name) \
  lclosure_t name ## _f = {.type = LUAF_CFUNCTION, .function.c = name}

void vm_run(lfunc_t *fun);

#endif /* _VM_H_ */
