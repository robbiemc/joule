#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>
#include "lstring.h"

typedef uint64_t lvalue;

typedef struct lfunc {
  lstring_t     *name;
  int           start_line;
  int           end_line;
  uint8_t       num_upvalues;
  uint8_t       num_parameters;
  uint8_t       is_vararg;
  uint8_t       max_stack;

  size_t        num_instrs;
  uint32_t      *instrs;
  size_t        num_consts;
  lvalue        *consts;
  size_t        num_funcs;
  struct lfunc  *funcs;

  // raw pointers to debug information - no parsing is done for these
  void          *dbg_lines;
  void          *dbg_locals;
  void          *dbg_upvalues;
} lfunc_t;

extern const lvalue LV_NIL;
extern const lvalue LV_TRUE;
extern const lvalue LV_FALSE;

lvalue lv_bool(u8 v);
lvalue lv_number(u64 v);
lvalue lv_string(lstring_t *v);

#endif /* _VM_H_ */
