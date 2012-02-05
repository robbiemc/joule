#ifndef _LSTATE_H
#define _LSTATE_H

#define LSTATE u32 argc, u32 argvi, u32 retc, u32 retvi

#include "error.h"
#include "luav.h"
#include "vm.h"

#define lstate_getarg(n, e) \
  ({ if ((n) >= argc) { err_missing(n, e); } vm_stack.base[argvi + n]; })
#define lstate_getval(n) \
  ({ if ((n) >= argc) { err_missing(n, LANY); } vm_stack.base[argvi + n]; })

#define lstate_getnumber(n)   lv_castnumber(lstate_getarg(n, LNUMBER), n)
#define lstate_getstring(n)   lv_caststring(lstate_getarg(n, LSTRING), n)
#define lstate_getfunction(n) lv_getfunction(lstate_getarg(n, LFUNCTION), n)
#define lstate_getbool(n)     lv_getbool(lstate_getarg(n, LBOOLEAN), n)
#define lstate_gettable(n)    lv_gettable(lstate_getarg(n, LTABLE), n)
#define lstate_getthread(n)   lv_getthread(lstate_getarg(n, LTHREAD), n)
#define lstate_getuserdata(n) lv_getuserdata(lstate_getarg(n, LUSERDATA), n)

#define lstate_return(v, n)                                 \
  do {                                                      \
    if ((n) < retc) {                                       \
      if (&vm_stack.base[retvi + (n)] >= vm_stack.top) {    \
        vm_stack_grow(&vm_stack, 3);                        \
      }                                                     \
      vm_stack.base[retvi + (n)] = v;                       \
    }                                                       \
  } while (0)
#define lstate_return1(v) { lstate_return(v, 0); return 1; }

#endif /* _LSTATE_H */
