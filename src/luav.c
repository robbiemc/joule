/**
 * @file luav.c
 * @brief Implementation of manipulating Lua values
 *
 * @see luav.h for explanation of NaN boxing
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "panic.h"
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
  if (!lv_istable(value)) { err_badtype(argnum, LTABLE, lv_gettype(value)); }
  return (lhash_t*) lv_getptr(value);
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
  if (!lv_isuserdata(value)) {
    err_badtype(argnum, LUSERDATA, lv_gettype(value));
  }
  return (void*) lv_getptr(value);
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
  if (!lv_isfunction(value)) {
    err_badtype(argnum, LFUNCTION, lv_gettype(value));
  }
  return (lclosure_t*) lv_getptr(value);
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
  if (!lv_isthread(value)) {
    err_badtype(argnum, LTHREAD, lv_gettype(value));
  }
  return (struct lthread*) lv_getptr(value);
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
  if (lv_isnumber(v1) && lv_isnumber(v2)) {
    double d1 = lv_cvt(v1);
    double d2 = lv_cvt(v2);
    if (d1 < d2) return -1;
    if (d1 > d2) return  1;
    /* Stupid hack to get around NaN comparisons always false */
    return isnan(d1);
  }

  // this has to go here because NaN != NaN
  if (v1 == v2) return 0;

  if (lv_isstring(v1) && lv_isstring(v2)) {
    lstring_t *s1 = (lstring_t*) lv_getptr(v1);
    lstring_t *s2 = (lstring_t*) lv_getptr(v2);
    size_t minlen = MIN(s1->length, s2->length);
    int cmp = memcmp(s1->data, s2->data, minlen);
    if (cmp != 0) return cmp;
    if (s1->length < s2->length) return -1;
    return s1->length > s2->length;
  }

  static char errbuf[100];
  sprintf(errbuf, "attempt to compare %s with %s",
          err_typestr(lv_gettype(v1)), err_typestr(lv_gettype(v2)));
  err_rawstr(errbuf, TRUE);
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
  if (lv_isnumber(number)) {
    return lv_cvt(number);
  } else if (!lv_isstring(number)) {
    err_badtype(argnum, LNUMBER, lv_gettype(number));
  }

  lstring_t *str = (lstring_t*) lv_getptr(number);
  double num;
  if (lv_parsenum(str, base, &num) < 0) {
    err_badtype(argnum, LNUMBER, LSTRING);
  }
  return num;
}

int lv_parsenum(lstring_t *str, u32 base, double *value) {
  char *end;
  double num = base == 10 ? strtod(str->data, &end) :
                            (double) strtoul(str->data, &end, (int) base);

  if (end == str->data) { return -1; }
  while (*end != 0 && isspace(*end)) end++;
  if (end == str->data + str->length) {
    *value = num;
    return 0;
  }
  return -1;
}

lstring_t* lv_caststring(luav number, u32 argnum) {
  if (lv_isstring(number)) {
    return (lstring_t*) lv_getptr(number);
  } else if (!lv_isnumber(number)) {
    err_badtype(argnum, LSTRING, lv_gettype(number));
  }

  lstring_t *str = lstr_alloc(20);
  str->length = (size_t)snprintf(str->data, 20, LUA_NUMBER_FMT, lv_cvt(number));
  return lstr_add(str);
}

/**
 * @brief Concatenate two lua-values
 *
 * Does not perform any metamethods if available, will cause error if the two
 * values are not strings or numbers.
 *
 * @param v1 the first value
 * @param v2 the second value
 * @return the concatenated string
 */
luav lv_concat(luav v1, luav v2) {
  lstring_t *s1 = lv_caststring(v1, 0);
  lstring_t *s2 = lv_caststring(v2, 0);
  lstring_t *sn = lstr_alloc(s1->length + s2->length);
  printf("%p %p %p\n", s1, s2, sn);
  memcpy(sn->data, s1->data, s1->length);
  memcpy(sn->data + s1->length, s2->data, s2->length);
  sn->data[s1->length + s2->length] = 0;
  return lv_string(lstr_add(sn));
}
