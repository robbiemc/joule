#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

#include "lstring.h"
#include "luav.h"

struct lhash;
extern struct lhash lua_globals;

typedef u32 cvararg_t(u32, luav*, u32, luav*);
typedef luav conearg_t(luav);
typedef luav ctwoarg_t(luav, luav);

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
    cvararg_t *vararg;
    conearg_t *onearg;
    ctwoarg_t *twoarg;
  } function;
  luav upvalues[0];
} lclosure_t;

#define LUAF_C_VARARG 0
#define LUAF_C_1ARG   1
#define LUAF_C_2ARG   2
#define LUAF_LUA      3
#define CLOSURE_SIZE(num_upvalues) \
  (sizeof(lclosure_t) + (num_upvalues) * sizeof(luav))

#define LUAF_CLOSURE(typ, fun, field) \
  lclosure_t fun ## _f = {.type = typ, .function.field = fun}
#define LUAF_1ARG(name) LUAF_CLOSURE(LUAF_C_1ARG, name, onearg)
#define LUAF_2ARG(name) LUAF_CLOSURE(LUAF_C_2ARG, name, twoarg)
#define LUAF_VARARG(name) LUAF_CLOSURE(LUAF_C_VARARG, name, vararg)

void vm_run(lfunc_t *fun);

#endif /* _VM_H_ */
