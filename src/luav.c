/**
 * @file luav.c
 * @brief Implementation of manipulating Lua values
 *
 * @see luav.h for explanation of NaN boxing
 */

#include <assert.h>
#include <math.h>

#include "config.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "vm.h"

#define LV_CPL(typ) (0xf - (typ))

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
  double cvt = lv_cvt(value);
  assert(lv_gettype(value) == LNUMBER);
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
  return msw & 0xf;
}
