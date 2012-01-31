/**
 * @file luav.c
 * @brief Implementation of manipulating Lua values
 *
 * @see luav.h for explanation of NaN boxing
 */

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "vm.h"

/**
 * @brief Get the table associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a table
 *
 * @param value the lua value which is a table
 * @return the pointer to the table struct
 */
lhash_t* lv_gettable(luav value, u32 argnum) {
  assert(lv_gettype(value) == LTABLE);
  return (lhash_t*) LUAV_DATA(value);
}

/**
 * @brief Get the boolean associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a bool
 *
 * @param value the lua value which is a table
 * @return the pointer to the table struct
 */
u8 lv_getbool(luav value, u32 argnum) {
  return value != LUAV_NIL && value != LUAV_FALSE;
}

/**
 * @brief Get the user data associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a user data
 *
 * @param value the lua value which is a user data
 * @return the pointer to the data
 */
void* lv_getuserdata(luav value, u32 argnum) {
  assert(lv_gettype(value) == LUSERDATA);
  return (void*) LUAV_DATA(value);
}

/**
 * @brief Get the function associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a function
 *
 * @param value the lua value which is a function
 * @return the pointer to the function
 */
lclosure_t* lv_getfunction(luav value, u32 argnum) {
  assert(lv_gettype(value) == LFUNCTION);
  return (lclosure_t*) LUAV_DATA(value);
}

/**
 * @brief Get the thread associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a thread
 *
 * @param value the lua value which is a thread
 * @return the thread pointer
 */
struct lthread* lv_getthread(luav value, u32 argnum) {
  assert(lv_gettype(value) == LTHREAD);
  return (struct lthread*) LUAV_DATA(value);
}

/**
 * @brief Get the upvalue associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not an upvalue
 *
 * @param value the lua value which is an upvalue
 * @return the pointer to the luav the upvalue stands for
 */
upvalue_t* lv_getupvalue(luav value) {
  assert(lv_gettype(value) == LUPVALUE);
  return (upvalue_t*) LUAV_DATA(value);
}

/**
 * @brief Hash a lua value
 *
 * @param value the object or reference or value to hash
 * @return the hash for the given value
 */
u32 lv_hash(luav value) {
  /* Most values have some form of entropy somewhere in their 64 bits. By taking
     both halves into account, we get a quick and dirty hash function which
     works fairly well. This hinges upon the fact that two values are equal
     if and only if the object they represent are the same */
  return (u32) (value ^ (value >> 32));
}

/**
 * @brief Returns the type of the luav
 *
 * @param value the luav to get the type of
 * @return the type
 */
u8 lv_gettype(luav value) {
  u32 msw = (u32) (value >> LUAV_DATA_SIZE);
  if ((msw & 0xfff0) != 0xfff0)
    return LNUMBER;
  u8 typ = msw & 0xf;
  if (typ == 0) {
    return LNUMBER;
  }
  return typ;
}

/**
 * @brief Compare two lua values
 *
 * @param v1 the first value
 * @param v2 the second value
 * @return <0 if v1 < v2, 0 if v1 == v2, >0 if v1 > v2
 */
int lv_compare(luav v1, luav v2) {
  u8 type = lv_gettype(v1);
  assert(type == lv_gettype(v2));

  if (type == LNUMBER) {
    double d1 = lv_castnumber(v1, 0);
    double d2 = lv_castnumber(v2, 1);
    if (d1 < d2) {
      return -1;
    } else if (d1 > d2) {
      return 1;
    }
    /* Stupid hack to get around NaN comparisons always false */
    return isnan(d1);
  } else if (v1 == v2) {
    return 0;
  }

  /* getstring will panic if these aren't strings */
  lstring_t *s1 = lv_caststring(v1, 0);
  lstring_t *s2 = lv_caststring(v2, 1);
  size_t minlen = s1->length < s2->length ? s1->length : s2->length;
  int cmp = memcmp(s1->ptr, s2->ptr, minlen);
  if (cmp != 0) {
    return cmp;
  } else if (s1->length < s2->length) {
    return -1;
  } else if (s1->length > s2->length){
    return 1;
  } else {
    return 0;
  }
}

/**
 * @brief Cast a luav into a number
 *
 * Strings are parsed as numbers, numbers are just returned, and everything else
 * is returned as nil. Malformed strings also return nil.
 *
 * @param number the value to cast
 * @param base the base to parse a string as
 * @return the value casted as a number
 */
double lv_castnumberb(luav number, u32 base, u32 argnum) {
  u8 type = lv_gettype(number);
  if (type == LNUMBER) {
    return lv_cvt(number);
  } else if (type != LSTRING) {
    err_badtype(argnum, LNUMBER, type);
  }

  lstring_t *str = lv_caststring(number, argnum);
  double num;
  if (lv_parsenum(str, base, &num) < 0) {
    err_badtype(argnum, LNUMBER, LSTRING);
  }
  return num;
}

int lv_parsenum(lstring_t *str, u32 base, double *value) {
  char *end;
  double num = base == 10 ? strtod(str->ptr, &end) :
                            (double) strtoul(str->ptr, &end, (int) base);

  if (end == str->ptr) { return -1; }
  while (*end != 0 && isspace(*end)) end++;
  if (end == str->ptr + str->length) {
    *value = num;
    return 0;
  }
  return -1;
}

lstring_t* lv_caststring(luav number, u32 argnum) {
  u8 type = lv_gettype(number);
  if (type == LSTRING) {
    return lstr_get(LUAV_DATA(number));
  } else if (type != LNUMBER) {
    err_badtype(argnum, LSTRING, type);
  }

  char *buf = xmalloc(20);
  int len = snprintf(buf, 20, LUA_NUMBER_FMT, lv_castnumber(number, argnum));
  return lstr_get(lstr_add(buf, (size_t) len, TRUE));
}
