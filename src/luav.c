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
 * @brief Return an instance of the value which represents LUAV_NIL
 */
luav lv_nil() {
  return LUAV_NAN_MASK | LUAV_NIL;
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
  return LUAV_SETDATA(LUAV_NAN_MASK | LTABLE, (u64) data);
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
         value == lv_bits(0.0 / 0.0));
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
 * @brief Dump a lua value to stdout for debugging
 *
 * @param value lua value to print out
 */
void lv_dump(luav value) {
  if ((value & LUAV_NAN_MASK) == LUAV_NAN_MASK) {
    printf("%f\n", lv_cvt(value));
    return;
  }

  u64 data = LUAV_DATA(value);

  switch (value & LUAV_TYPE_MASK) {
    case LNUMBER:     assert(0 && "LNUMBER souldn't exist really?");
    case LSTRING:     printf("{string...}");                return;
    case LTABLE:      printf("{table...}");                return;
    case LBOOLEAN:    printf(data ? "true\n" : "false\n");  return;
    case LNIL:        printf("nil\n");                      return;
  }

  printf("Bad luav type: %lld\n", value & LUAV_TYPE_MASK);
  assert(0 && "Bad luav type");
}
