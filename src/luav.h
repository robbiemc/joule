#ifndef _LUA_H_
#define _LUA_H_

#include "lstring.h"

/**
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
 * bits[51:4] - 48 bits to hold a value, defined below
 * bits[ 3:3] - up for grabs
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

/* Must fit in 3 bits, 0-7 */
#define LNUMBER  0
#define LSTRING  1
#define LTABLE   2
#define LBOOLEAN 3
#define LNIL     4

#define LUAV_TYPE_BITS 3
#define LUAV_DATA_MASK 0x0000ffffffffffff

#define LUAV_DATA(bits) (((bits) >> LUAV_TYPE_BITS) & LUAV_DATA_MASK)
#define LUAV_SETDATA(bits, data) \
  ((((data) & LUAV_DATA_MASK) << LUAV_TYPE_BITS) | (bits))

typedef double luav;

luav lv_nil(void);
luav lv_bool(u8 v);
luav lv_number(u64 v);
luav lv_string(lstring_t *v);

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

#endif /* _LUA_H_ */
