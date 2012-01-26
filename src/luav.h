/**
 * @file luav.h
 * @brief Headers for definitions related to manipulating Lua values
 *
 * NaN boxing ensues, 64 bits for a double --
 *
 *  |s|  11 bits - e | 52 bits - f |
 *
 * Infinity is when e = 0x7ff and f = 0.
 * NaN is when e = 0x7ff and f != 0.
 * Must avoid hardware NaN, however, which is e = 0x7ff, f = 0x8000000000000.
 *
 * Basically, we have 51 free bits in a double (possibly 52 if the sign bit is
 * used).
 *
 * Of these 51 bits, the definitions are as follows
 *
 * bits[50:3] - 48 bits to hold a value, defined below
 * bits[ 2:0] - type information
 *
 * LSTRING, LTABLE - 48 bits are for the pointer to the data. It turns out that
 *                   although we have a 64-bit address space possibly, only 48
 *                   bits of memory are actually allocated. The virtual address
 *                   is the sign-extended version of these 64 bits. Hence, we
 *                   can use 48 bits to completely store the memory address.
 *
 * LNUMBER - this type is a bit special because numbers are all implemented
 *           as doubles. If the double is NOT NaN, then the double is the actual
 *           value of the number. Otherwise, if it is NaN, then it should
 *           literally be NaN.
 *
 * LBOOLEAN - 0 for false, 1 for true in 48 bits
 */

#ifndef _LUAV_H_
#define _LUAV_H_

#include "config.h"

/**
 * All values are encoded in 64 bits. This is the same size as a double, and
 * all values that are not doubles will be encoded as a form of NaN
 */
typedef u64 luav;

#include <stdlib.h>

#include "lstring.h"

struct lclosure;
struct lhash;

/* Must fit in 3 bits, 0-7 */
#define LSTRING   0
#define LUPVALUE  1
#define LTABLE    2
#define LBOOLEAN  3
#define LNIL      4
#define LFUNCTION 5
#define LTHREAD   6
#define LUSERDATA 7
#define LNUMBER   8 /* not actually in the bits, encoded as not NaN */

/* Macros for dealing with u64 bits for luav */
#define LUAV_NAN_MASK  0x7ff0000000000000LL
#define LUAV_NIL (LUAV_NAN_MASK | LNIL)
#define LUAV_TYPE_BITS 3
#define LUAV_TYPE_MASK ((1 << LUAV_TYPE_BITS) - 1)
#define LUAV_DATA_MASK 0x0000ffffffffffffLL

#define LUAV_DATA(bits) (((bits) >> LUAV_TYPE_BITS) & LUAV_DATA_MASK)
#define LUAV_SETDATA(bits, data) \
  ((((data) & LUAV_DATA_MASK) << LUAV_TYPE_BITS) | (bits))
#define LUAV_ISNUM(bits) \
    ((bits) & LUAV_NAN_MASK != LUAV_NAN_MASK || (bits) ==)

/* TODO: thread */
luav lv_number(double v);
luav lv_table(struct lhash *hash);
luav lv_bool(u8 v);
luav lv_userdata(void *data);
luav lv_string(lstr_idx idx);
luav lv_function(struct lclosure *fun);
luav lv_upvalue(luav *ptr);

/* TODO: getthread */
double           lv_getnumber(luav value);
struct lhash*    lv_gettable(luav value);
u8               lv_getbool(luav value);
void*            lv_getuserdata(luav value);
size_t           lv_getstring(luav value);
struct lclosure* lv_getfunction(luav value);
luav*            lv_getupvalue(luav value);

u8  lv_gettype(luav value);
u32 lv_hash(luav value);

static inline double lv_cvt(u64 bits) {
  union { double converted; u64 bits; } cvt;
  cvt.bits = bits;
  return cvt.converted;
}

static inline u64 lv_bits(double value) {
  union { double converted; u64 bits; } cvt;
  cvt.converted = value;
  return cvt.bits;
}

#endif /* _LUAV_H_ */
