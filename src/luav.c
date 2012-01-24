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
  return LUAV_SETDATA(LBOOLEAN, !!v);
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

/* TODO: solidify this */
luav lv_string(lstring_t *v) {
  return LUAV_SETDATA(LSTRING, (u64) v);
}

/**
 * @brief Return an instance of the value which represents LUAV_NIL
 */
luav lv_nil() {
  return LUAV_NIL;
}

/**
 * @brief Convert a hash table to the lua value for the table
 *
 * @param hash the allocated table to convert to a value
 * @return the lua value representing the hash table
 */
luav lv_table(lhash_t *hash) {
  return LUAV_SETDATA(LTABLE, (u64) hash);
}

/**
 * @brief Convert some user data to a lua value
 *
 * @param data raw data to pass around in lua, cannot be modified
 * @return the lua value representing the pointer
 */
luav lv_userdata(void *data) {
  return LUAV_SETDATA(LTABLE, (u64) data);
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
    case LTABLE:      printf("{string...}");                return;
    case LBOOLEAN:    printf(data ? "true\n" : "false\n");  return;
    case LNIL:        printf("nil\n");                      return;
  }

  printf("Bad luav type: %lld\n", (long long int) (value & LUAV_TYPE_MASK));
  assert(0 && "Bad luav type");
}
