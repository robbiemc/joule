/**
 * @file luav.h
 * @brief Headers for definitions related to manipulating Lua values
 *
 * A luav is a nan-boxed double interpreted as a u64 everywhere. Encodings of
 * doubles allow us to encode all types which are not numbers as NaN. This is
 * because the machine only generates one actual value for NaN, even though
 * there are many values representing NaN.
 */

#ifndef _LUAV_H_
#define _LUAV_H_

#include "config.h"

/**
 * All values are encoded in 64 bits. This is the same size as a double, and
 * all values that are not doubles will be encoded as a form of NaN
 */
typedef u64 luav;

#include <inttypes.h>
#include <stdlib.h>

#include "lstring.h"

struct lclosure;
struct lhash;
struct lthread;

/*
 * Must fit in 4 bits - 0 and 8 are special
 *
 * 0 could be interpreted as infinity, so it can't be used as a type value
 * 8 could be the actual NaN, so we need this to be the type Number type
 *
 * nil is defined as 15 (1111b), so its value in a luav is 0xFFFFFFFF...,
 * which can be checked quickly
 */
#define LBOOLEAN  UINT64_C(1)
#define LSTRING   UINT64_C(2)
#define LFUNCTION UINT64_C(3)
#define LTABLE    UINT64_C(4)
#define LUSERDATA UINT64_C(5)
#define LTHREAD   UINT64_C(6)
#define LUPVALUE  UINT64_C(7)
#define LNUMBER   UINT64_C(8)
#define LNIL      UINT64_C(15)
#define LANY      UINT64_C(16)
/* Used for garbage collection, not in types */
#define LFUNC     100
#define LCFUNC    101

/* Macros for dealing with u64 bits for luav */
#define LUAV_DATA_SIZE 48
#define LUAV_NAN_MASK  UINT64_C(0xfff0000000000000)
#define LUAV_TYPE_MASK UINT64_C(0xffff000000000000)
#define LUAV_NAN       UINT64_C(0xfff8000000000000)
#define LUAV_NIL       UINT64_C(0xffffffffffffffff)
#define LUAV_TRUE      (LUAV_NAN_MASK | ((u64) LBOOLEAN << LUAV_DATA_SIZE) | 1)
#define LUAV_FALSE     (LUAV_NAN_MASK | ((u64) LBOOLEAN << LUAV_DATA_SIZE) | 0)
#define LUAV_DATA_MASK ((UINT64_C(1) << LUAV_DATA_SIZE) - 1)

#define LUAV_DATA(bits) ((bits) & LUAV_DATA_MASK)
#define LUAV_PACK(typ, data) \
  ((LUAV_NAN_MASK | ((u64)(typ) << 48)) | ((u64)(data) & LUAV_DATA_MASK))

#define lv_hastyp(lv, typ) (((typ) | LUAV_NAN_MASK) == ((lv) & LUAV_TYPE_MASK))

/* A number is either 0x8 or 0x0, so so long as the lower 3 bits of the type
   are 0, then this can be considered a number */
#define lv_isnumber(lv)   (((lv) & LUAV_NAN_MASK) != LUAV_NAN_MASK || \
                           !((lv) & (UINT64_C(7) << LUAV_DATA_SIZE)))
#define lv_isstring(lv)   lv_hastyp(lv, LSTRING << LUAV_DATA_SIZE)
#define lv_istable(lv)    lv_hastyp(lv, LTABLE << LUAV_DATA_SIZE)
#define lv_isbool(lv)     lv_hastyp(lv, LBOOLEAN << LUAV_DATA_SIZE)
#define lv_isuserdata(lv) lv_hastyp(lv, LUSERDATA << LUAV_DATA_SIZE)
#define lv_isfunction(lv) lv_hastyp(lv, LFUNCTION << LUAV_DATA_SIZE)
#define lv_isupvalue(lv)  lv_hastyp(lv, LUPVALUE << LUAV_DATA_SIZE)
#define lv_isthread(lv)   lv_hastyp(lv, LTHREAD << LUAV_DATA_SIZE)

/* Boxing a luav */
#define lv_ptr(p, typ)    LUAV_PACK(typ, (size_t) (p))
#define lv_number(n)      lv_bits(n)
#define lv_table(hash)    lv_ptr(hash, LTABLE)
#define lv_bool(v)        LUAV_PACK(LBOOLEAN, !!(v))
#define lv_userdata(data) lv_ptr(data, LUSERDATA)
#define lv_string(idx)    lv_ptr(idx, LSTRING)
#define lv_function(fun)  lv_ptr(fun, LFUNCTION)
#define lv_upvalue(up)    lv_ptr(up, LUPVALUE)
#define lv_thread(thread) lv_ptr(thread, LTHREAD)

/* Unboxing a luav */
double           lv_castnumberb(luav value, u32 base, u32 argnum);
struct lstring*  lv_caststring(luav value, u32 argnum);
struct lhash*    lv_gettable(luav value, u32 argnum);
u8               lv_getbool(luav value, u32 argnum);
void*            lv_getuserdata(luav value, u32 argnum);
struct lclosure* lv_getfunction(luav value, u32 argnum);
struct lthread*  lv_getthread(luav value, u32 argnum);
#define lv_getbool(v, _) ((u8) ((v) != LUAV_NIL && (v) != LUAV_FALSE))
#define lv_getupvalue(v) \
  ({ assert(lv_isupvalue(v)); (luav*) (size_t) LUAV_DATA(v); })

#define lv_getptr(v)  ((void*) (size_t) ((v) & LUAV_DATA_MASK))

int lv_parsenum(struct lstring *str, u32 base, double *value);

#define lv_hash(lv) (((u32) (lv) ^ (u32) ((lv) >> 32) ^ 0xfc83d6a5))
#define lv_nilify(mem, count) memset((mem), 0xff, (count) * sizeof(luav))
#define lv_sametyp(v1, v2) (((v1) & LUAV_TYPE_MASK) == ((v2) & LUAV_TYPE_MASK))

u8   lv_gettype(luav value);
int  lv_compare(luav v1, luav v2);
luav lv_concat(luav v1, luav v2);

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

static inline double lv_castnumber(luav n, u32 argnum) {
  if (lv_isnumber(n)) {
    return lv_cvt(n);
  }
  return lv_castnumberb(n, 10, argnum);
}

#endif /* _LUAV_H_ */
