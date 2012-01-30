/**
 * @file luav.c
 * @brief Implementation of manipulating Lua values
 *
 * @see luav.h for explanation of NaN boxing
 */

#include <assert.h>
#include <math.h>
#include <string.h>
#include <wctype.h>

#include "config.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "panic.h"
#include "vm.h"

/**
 * @brief Convert an 8 bit value to a lua boolean
 *
 * @param v the value to convert (0 = false, nonzero = true)
 * @return the lua value for the corresponding boolean
 */
luav lv_bool(u8 v) {
  return LUAV_PACK(LBOOLEAN, !!v);
}

/**
 * @brief Convert a floating point number to a lua number
 *
 * @param d the double-precision float to convert
 * @return the lua value for this number
 */
luav lv_number(double d) {
  return lv_bits(d);
}

/**
 * @brief Convert a hash table to the lua value for the table
 *
 * @param hash the allocated table to convert to a value
 * @return the lua value representing the hash table
 */
luav lv_table(lhash_t *hash) {
  return LUAV_PACK(LTABLE, (u64) hash);
}

/**
 * @brief Convert some user data to a lua value
 *
 * @param data raw data to pass around in lua, cannot be modified
 * @return the lua value representing the pointer
 */
luav lv_userdata(void *data) {
  return LUAV_PACK(LUSERDATA, (u64) data);
}

/**
 * @brief Convert a string index to a luav
 *
 * @param idx the index of the string in the global string array
 * @return the lua value representing the string
 */
luav lv_string(size_t idx) {
  return LUAV_PACK(LSTRING, idx);
}

/**
 * @brief Convert a function pointer to a luav
 *
 * @param fun the function to embed in a luav
 * @return the lua value representing the function
 */
luav lv_function(lclosure_t *fun) {
  return LUAV_PACK(LFUNCTION, (u64) fun);
}

/**
 * @brief Convert a luav to an upvalue
 *
 * Upvalues are actually pointers to the real luav. This way, the upvalue
 * can be shared across all functions which can access it, and any can return
 * without destroying the variable. Also, modifications are visible to everyone.
 *
 * @param value the pointer to the actual lua value
 * @return the lua value representing the function
 */
luav lv_upvalue(upvalue_t *ptr) {
  return LUAV_PACK(LUPVALUE, (u64) ptr);
}

/**
 * @brief Get the number associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a number
 *
 * @param value the lua value which is a number
 * @return the floating-point representation of the specified value.
 */
double lv_getnumber(luav value) {
  assert(lv_gettype(value) == LNUMBER);
  return lv_cvt(value);
}

/**
 * @brief Get the table associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a table
 *
 * @param value the lua value which is a table
 * @return the pointer to the table struct
 */
lhash_t* lv_gettable(luav value) {
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
u8 lv_getbool(luav value) {
  assert(lv_gettype(value) == LBOOLEAN);
  return (u8) LUAV_DATA(value);
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
void* lv_getuserdata(luav value) {
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
lclosure_t* lv_getfunction(luav value) {
  assert(lv_gettype(value) == LFUNCTION);
  return (lclosure_t*) LUAV_DATA(value);
}

/**
 * @brief Get the string associated with the given value
 *
 * It is considered a fatal error to call this function when the type of the
 * value is not a string
 *
 * @param value the lua value which is a string
 * @return the index of the string
 */
lstr_idx lv_getstring(luav value) {
  assert(lv_gettype(value) == LSTRING);
  return LUAV_DATA(value);
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
  if (v1 == v2) { return 0; }

  u8 type = lv_gettype(v1);
  assert(type == lv_gettype(v2));

  if (type == LNUMBER) {
    double d1 = lv_getnumber(v1);
    double d2 = lv_getnumber(v2);
    if (d1 < d2) {
      return -1;
    } else if (d1 > d2) {
      return 1;
    }
    panic("if num(v1) = num(v2), then v1 should = v2");
  }

  /* getstring will panic if these aren't strings */
  lstring_t *s1 = lstr_get(lv_getstring(v1));
  lstring_t *s2 = lstr_get(lv_getstring(v2));
  if (s1->length < s2->length) {
    return -1;
  } else if (s1->length > s2->length) {
    return 1;
  }
  return memcmp(s1->ptr, s2->ptr, s1->length);
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
luav lv_tonumber(luav number, int base) {
  u8 type = lv_gettype(number);
  if (type == LNUMBER) {
    return number;
  } else if (type != LSTRING) {
    return LUAV_NIL;
  }

  lstring_t *str = lstr_get(lv_getstring(number));
  char *end;
  double num;

  if (base == 10) {
    num = strtod(str->ptr, &end);
  } else {
    num = (double) strtoul(str->ptr, &end, base);
  }

  while (*end != 0 && iswspace(*end)) end++;
  if (end == str->ptr + str->length && str->length > 0) {
    return lv_number(num);
  }
  return LUAV_NIL;
}

/**
 * @brief Coerce a lua value into a boolean
 *
 * Only false and nil are coerced to false, everything else is true.
 *
 * @param value the value to coerce
 * @return the coerced boolean
 */
luav lv_tobool(luav value) {
  return value == LUAV_NIL || value == LUAV_FALSE ? LUAV_FALSE : LUAV_TRUE;
}
