#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

#include "lstring.h"
#include "luav.h"

struct lhash;
extern struct lhash lua_globals;

typedef u32 cvarret_t(u32, luav*, u32, luav*);
typedef luav cnoarg_t(void);
typedef luav conearg_t(luav);
typedef luav ctwoarg_t(luav, luav);
typedef luav cthreearg_t(luav, luav, luav);
typedef u32 cboundarg_t(luav, u32, luav*, u32, luav*);
typedef luav cvararg_t(u32, luav*);

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
    cnoarg_t *noarg;
    conearg_t *onearg;
    ctwoarg_t *twoarg;
    cthreearg_t *threearg;
    cboundarg_t *boundarg;
    cvarret_t *varret;
  } function;
  luav upvalues[0];
} lclosure_t;

#define LUAF_NIL      0
#define LUAF_C_VARARG 1
#define LUAF_C_0ARG   2
#define LUAF_C_1ARG   3
#define LUAF_C_2ARG   4
#define LUAF_C_3ARG   5
#define LUAF_C_VARRET 6
#define LUAF_C_BOUND  7
#define LUAF_LUA      8
#define CLOSURE_SIZE(num_upvalues) \
  (sizeof(lclosure_t) + (num_upvalues) * sizeof(luav))

#define LUAF_CLOSURE(typ, fun, field) \
  lclosure_t fun ## _f = {.type = typ, .function.field = fun}
#define LUAF_0ARG(name) LUAF_CLOSURE(LUAF_C_0ARG, name, noarg)
#define LUAF_1ARG(name) LUAF_CLOSURE(LUAF_C_1ARG, name, onearg)
#define LUAF_2ARG(name) LUAF_CLOSURE(LUAF_C_2ARG, name, twoarg)
#define LUAF_3ARG(name) LUAF_CLOSURE(LUAF_C_3ARG, name, threearg)
#define LUAF_VARARG(name) LUAF_CLOSURE(LUAF_C_VARARG, name, vararg)
#define LUAF_BOUND(name)  LUAF_CLOSURE(LUAF_C_BOUND, name, boundarg)
#define LUAF_VARRET(name) LUAF_CLOSURE(LUAF_C_VARRET, name, varret)

void vm_run(lfunc_t *fun);
u32 vm_fun(lclosure_t *c, u32 argc, luav *argv, u32 retc, luav *retv);

#endif /* _VM_H_ */
