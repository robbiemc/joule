/**
 * @file luav.c
 * @brief Implementation of manipulating Lua values
 *
 * @see luav.h for explanation of NaN boxing
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "config.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"

/* Macros for dealing with u64 bits for luav */
#define LUAV_TYPE_BITS 3
#define LUAV_TYPE_MASK ((1 << LUAV_TYPE_BITS) - 1)
#define LUAV_DATA_MASK 0x0000ffffffffffffLL

#define LUAV_DATA(bits) (((bits) >> LUAV_TYPE_BITS) & LUAV_DATA_MASK)
#define LUAV_SETDATA(bits, data) \
  ((((data) & LUAV_DATA_MASK) << LUAV_TYPE_BITS) | (bits))
#define LUAV_ISNUM(bits) \
    ((bits) & LUAV_NAN_MASK != LUAV_NAN_MASK || (bits) ==)

/**
 * @brief Convert an 8 bit value to a lua boolean
 *
 * @param v the value to convert (0 = false, nonzero = true)
 * @return the lua value for the corresponding boolean
 */
luav lv_bool(u8 v) {
  return LUAV_SETDATA(LUAV_NAN_MASK | LBOOLEAN, !!v);
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
  return LUAV_SETDATA(LUAV_NAN_MASK | LTABLE, (u64) hash);
}

/**
 * @brief Convert some user data to a lua value
 *
 * @param data raw data to pass around in lua, cannot be modified
 * @return the lua value representing the pointer
 */
luav lv_userdata(void *data) {
  return LUAV_SETDATA(LUAV_NAN_MASK | LUSERDATA, (u64) data);
}

/**
 * @brief Convert a string index to a luav
 *
 * @param idx the index of the string in the global string array
 * @return the lua value representing the string
 */
luav lv_string(size_t idx) {
  return LUAV_SETDATA(LUAV_NAN_MASK | LSTRING, idx);
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
  double cvt = lv_cvt(value);
  /* We're a number if we're infinite, not NaN, or the one machine NaN */
  assert(isinf(cvt) || (value & LUAV_NAN_MASK) != LUAV_NAN_MASK ||
         value == lv_bits(NAN));
  return cvt;
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
  assert((value & LUAV_TYPE_MASK) == LTABLE);
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
  assert((value & LUAV_TYPE_MASK) == LBOOLEAN);
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
  assert((value & LUAV_TYPE_MASK) == LUSERDATA);
  return (void*) LUAV_DATA(value);
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
size_t lv_getstring(luav value) {
  assert((value & LUAV_TYPE_MASK) == LSTRING);
  return LUAV_DATA(value);
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
  // check if it's a number
  double cvt = lv_cvt(value);
  if (!isnan(cvt) || isinf(cvt) || value == lv_bits(NAN))
    return LNUMBER;
  u8 type = (u8) (value & LUAV_TYPE_MASK);
  assert(type != LNUMBER);
  return type;
}
