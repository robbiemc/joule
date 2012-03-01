/**
 * @file lib/table.c
 * @brief Implementation of the lua 'table' module.
 *
 * Implementes the global 'table' table, adding all functions to it to operate
 * on hashes.
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "gc.h"
#include "lhash.h"
#include "lstate.h"
#include "vm.h"

static lhash_t *lua_table;
static u32 lua_table_getn(LSTATE);
static u32 lua_table_maxn(LSTATE);
static u32 lua_table_insert(LSTATE);
static u32 lua_table_remove(LSTATE);
static u32 lua_table_sort(LSTATE);
static u32 lua_table_concat(LSTATE);

INIT void lua_table_init() {
  lua_table = gc_alloc(sizeof(lhash_t), LTABLE);
  lhash_init(lua_table);

  cfunc_register(lua_table, "getn",   lua_table_getn);
  cfunc_register(lua_table, "maxn",   lua_table_maxn);
  cfunc_register(lua_table, "insert", lua_table_insert);
  cfunc_register(lua_table, "remove", lua_table_remove);
  cfunc_register(lua_table, "sort",   lua_table_sort);
  cfunc_register(lua_table, "concat", lua_table_concat);

  lhash_set(lua_globals, LSTR("table"), lv_table(lua_table));
}

/**
 * @brief Same as the '#' operator for a table
 *
 * Apparently this function is deprecated in 5.1, but we're implementing it for
 * test compatibility
 *
 * @param table the lua hash table to get the length of
 * @return the length of the table, as specified by '#'
 */
static u32 lua_table_getn(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  luav key, value;
  lhash_next(table, LUAV_NIL, &key, &value);
  lstate_return1(lv_number((double) table->length));
}

/**
 * @brief Calculates the maximum positive numerical indice for a given table
 *
 * @param table the lua table to search
 * @return the maximum positive numerical indice, or 0 if there are none
 *
 * @see lhash_maxn
 */
static u32 lua_table_maxn(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  lstate_return1(lv_number(lhash_maxn(table)));
}

/**
 * @brief Inserts a value into a table at the specified position.
 *
 * This makes the table act like an array, where all keys at and above the
 * specified index are shifted up to make room for this key.
 *
 * @param table the table to insert into
 * @param [pos] optional position parameter, defaults to (#table + 1)
 * @param value the value to insert
 */
static u32 lua_table_insert(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  u32 pos;
  luav value;
  if (argc > 2) {
    pos = (u32) lstate_getnumber(1);
    value = lstate_getval(2);
  } else {
    value = lstate_getval(1);
    pos = (u32) table->length + 1;
  }

  lhash_insert(table, pos, value);

  return 0;
}

/**
 * @brief Removes a value in a table at a specified position
 *
 * This makes the table act like an array, removing the element at the specified
 * position and shifting all future contiguous elements down by one.
 *
 * @param table the table to remove from
 * @param [pos] optional position parameter, defaults to (#table)
 */
static u32 lua_table_remove(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  u32 pos = (u32) table->length;
  if (argc > 1) {
    pos = (u32) lstate_getnumber(1);
  }

  lstate_return1(lhash_remove(table, pos));
}

/* Helper function to call in lua when sorting table entries */
static lclosure_t *sorter;

/**
 * @brief Internal helper to invoke a lua function which compares two lua
 *        values
 */
static int lua_invoke_compare(luav *v1, luav *v2) {
  if (sorter == NULL) {
    return lv_compare(*v1, *v2);
  }
  u32 idx = vm_stack_alloc(vm_stack, 2);
  vm_stack->base[idx] = *v1;
  vm_stack->base[idx + 1] = *v2;
  u32 ret = vm_fun(sorter, vm_running, 2, idx, 1, idx);
  if (ret == 0) {
    err_rawstr("Not enough return values from comparator", TRUE);
  }
  int cmp = lv_getbool(vm_stack->base[idx], 0);
  vm_stack_dealloc(vm_stack, idx);
  return cmp ? -1 : 1;
}

/**
 * @brief Sorts an array with an optionally provided function
 *
 * @param table the table to sort
 * @param [comp] optional comparator
 */
static u32 lua_table_sort(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  if (argc > 1) {
    sorter = lstate_getfunction(1);
  } else {
    sorter = NULL;
  }

  lhash_sort(table, lua_invoke_compare);
  return 0;
}

/**
 * @brief Concatenates elements in the array portion of a table
 *
 * @param table the table to access and concatenate
 * @param [sep = ""]    the separator to use
 * @param [i = 1]       starting index for concatentation
 * @param [j = #table]  ending index for concatenation
 */
static u32 lua_table_concat(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  lstring_t *sep = argc > 1 ? lv_caststring(lstate_getval(1), 0) : lstr_empty();
  u32 i = argc > 2 ? (u32) lstate_getnumber(2) : 1;
  u32 j = argc > 3 ? (u32) lstate_getnumber(3) : (u32) table->length;
  u32 k;

  if (i > j) {
    lstate_return1(lv_string(lstr_empty()));
  }

  size_t str_size = 0;
  for (k = i; k <= j && k <= table->length; k++) {
    lstring_t *str = lv_caststring(table->array[k], 0);
    str_size += str->length + sep->length;
  }
  str_size -= sep->length;

  lstring_t *ret = lstr_alloc(str_size);
  char *str_data = ret->data;
  for (k = i; k <= j && k <= table->length; k++) {
    lstring_t *str = lv_caststring(table->array[k], 0);
    memcpy(str_data, str->data, str->length);
    str_data += str->length;
    if (k != j && k < table->length) {
      memcpy(str_data, sep->data, sep->length);
      str_data += sep->length;
    }
  }
  *str_data = 0;

  ret = lstr_add(ret);
  lstate_return1(lv_string(ret));
}
