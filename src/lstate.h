#ifndef _LSTATE_H
#define _LSTATE_H

#include "error.h"
#include "luav.h"

#define LSTATE u32 argc, luav *argv, u32 retc, luav *retv

#define lstate_getarg(n, e) \
  ({ if ((n) >= argc) { err_missing(n, e); } argv[n]; })
#define lstate_getval(n) \
  ({ if ((n) >= argc) { err_missing(n, LANY); } argv[n]; })

#define lstate_getnumber(n)   lv_castnumber(lstate_getarg(n, LNUMBER), n)
#define lstate_getstring(n)   lv_caststring(lstate_getarg(n, LSTRING), n)
#define lstate_getfunction(n) lv_getfunction(lstate_getarg(n, LFUNCTION), n)
#define lstate_getbool(n)     lv_getbool(lstate_getarg(n, LBOOLEAN), n)
#define lstate_gettable(n)    lv_gettable(lstate_getarg(n, LTABLE), n)
#define lstate_getthread(n)   lv_getthread(lstate_getarg(n, LTHREAD), n)

#define lstate_return(v, n) do { if (n < retc) { retv[n] = v; } } while (0)
#define lstate_return1(v) { lstate_return(v, 0); return 1; }

#endif /* _LSTATE_H */
