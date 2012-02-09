/**
 * @file lstate.h
 * @brief Macros for dealing with the current lua state in functions
 */

#ifndef _LSTATE_H
#define _LSTATE_H

/**
 * @brief Macro for the arguments to any lua api function
 *
 * This macro should be used when declaring the arguments for a function, and
 * it summarizes the arguments to the function and what it should return.
 *
 * argc  - the number of arguments to the function
 * argvi - the index into the current stack of the base of the arguments, all
 *         future arguments are contiguous and above this index.
 * retc  - the desired number of return values
 * retvi - the index into the current stack of where to place return values. The
 *         number of return values generated must not exceed retc, and the stack
 *         might need to be grown to put the argument there.
 */
#define LSTATE u32 argc, u32 argvi, u32 retc, u32 retvi

#include "error.h"
#include "luav.h"
#include "vm.h"

/**
 * @brief Fetches an argument, with a helpful type error message
 *
 * @param n the index of the argument to fetch
 * @param e the expected type
 *
 * @note This function will cause error if there are not enough arguments
 *       provided.
 */
#define lstate_getarg(n, e) \
  ({ if ((n) >= argc) { err_missing(n, e); } vm_stack->base[argvi + n]; })

/**
 * @brief Fetches an argument, with a helpful type error message. Different from
 *        lstate_getarg in that the error message says that it just wants a
 *        value, not necessarily any type
 *
 * @param n the index of the argument to fetch
 *
 * @note This function will cause error if there are not enough arguments
 *       provided.
 */
#define lstate_getval(n) \
  ({ if ((n) >= argc) { err_missing(n, LANY); } vm_stack->base[argvi + n]; })

/**
 * Macros for getting C values from arguments. The return value is the
 * return value of each lv_{cast,get}* function, and there are two errors that
 * can possibly happen:
 *
 * 1. Not enough arguments
 * 2. Argument not of the specified type (or could not be casted)
 */
#define lstate_getnumber(n)   lv_castnumber(lstate_getarg(n, LNUMBER), n)
#define lstate_getstring(n)   lv_caststring(lstate_getarg(n, LSTRING), n)
#define lstate_getfunction(n) lv_getfunction(lstate_getarg(n, LFUNCTION), n)
#define lstate_getbool(n)     lv_getbool(lstate_getarg(n, LBOOLEAN), n)
#define lstate_gettable(n)    lv_gettable(lstate_getarg(n, LTABLE), n)
#define lstate_getthread(n)   lv_getthread(lstate_getarg(n, LTHREAD), n)
#define lstate_getuserdata(n) lv_getuserdata(lstate_getarg(n, LUSERDATA), n)

/**
 * @brief Macro for returning a value
 *
 * If the return index is greater than the amount of desired return values, the
 * value is not even calculated. If the return index is greater than the size
 * of the stack, the stack is grown by a fixed amount.
 *
 * @param v the luav to return
 * @param n the index at which to return this luav
 */
#define lstate_return(v, n)                                 \
  do {                                                      \
    if ((n) < retc) {                                       \
      if (&vm_stack->base[retvi + (n)] >= vm_stack->top) {  \
        vm_stack_grow(vm_stack, 3);                         \
      }                                                     \
      vm_stack->base[retvi + (n)] = v;                      \
    }                                                       \
  } while (0)

/**
 * @brief Helper for returning one value from a function
 *
 * This syntactically contains a 'return', so this can be at the end of a
 * function. Always evaluates the return value, regardless of whether it will be
 * returned or not.
 *
 * @param v the luav to return
 */
#define lstate_return1(v) {    \
    luav _tmp = v;             \
    lstate_return(_tmp, 0);    \
    return 1;                  \
  }

#endif /* _LSTATE_H */
