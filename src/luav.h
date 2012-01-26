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
 * Basically, we have 52 free bits in a double (possibly 53 if the sign bit is
 * used).
 *
 * Of these 52 bits, the definitions are as follows
 *
 * bits[47: 0] - 48 bits to hold a value, defined below
 * bits[51:48] - type information
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

/*
 * Must fit in 4 bits - 0 and 8 are special
 *
 * 0 could be interpreted as infinity, so it can't be used as a type value
 * 8 could be the actual NaN, so we need this to be the type Number type
 *
 * nil is defined as 15 (1111b), so its value in a luav is 0xFFFF0000...,
 * which can be checked quickly
 */
#define LBOOLEAN  1
#define LSTRING   2
#define LTABLE    3
#define LFUNCTION 4
#define LTHREAD   5
#define LUSERDATA 6
#define LUPVALUE  7
#define LNUMBER   8
#define LNIL      15

/* Macros for dealing with u64 bits for luav */
#define LUAV_DATA_SIZE 48
#define LUAV_NAN_MASK  0xfff0000000000000LL
#define LUAV_NIL (LUAV_NAN_MASK | ((u64)LNIL << LUAV_DATA_SIZE))
#define LUAV_DATA_MASK (((u64)1 << LUAV_DATA_SIZE) - 1)

#define LUAV_DATA(bits) ((bits) & LUAV_DATA_MASK)
#define LUAV_PACK(typ, data) \
  ((LUAV_NAN_MASK | ((u64)(typ) << 48)) | ((u64)(data) & LUAV_DATA_MASK))

typedef struct upvalue {
  u32 refcnt;
  luav value;
} upvalue_t;

/* TODO: thread */
luav lv_number(double v);
luav lv_table(struct lhash *hash);
luav lv_bool(u8 v);
luav lv_userdata(void *data);
luav lv_string(lstr_idx idx);
luav lv_function(struct lclosure *fun);
luav lv_upvalue(upvalue_t *ptr);

/* TODO: getthread */
double           lv_getnumber(luav value);
struct lhash*    lv_gettable(luav value);
u8               lv_getbool(luav value);
void*            lv_getuserdata(luav value);
size_t           lv_getstring(luav value);
struct lclosure* lv_getfunction(luav value);
upvalue_t*       lv_getupvalue(luav value);

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
