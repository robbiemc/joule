#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>
#include "lstring.h"
#include "luav.h"

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

  u32 (*cfunc)(u32 argc, luav *argv, u32 retc, luav *retv);
} lfunc_t;

#define LUA_FUNCTION(name) \
  lfunc_t name ## _f = {.cfunc = name}

void vm_run(lfunc_t *fun);

#endif /* _VM_H_ */
