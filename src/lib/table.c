/**
 * @file lib/table.c
 * @brief Implementation of the lua 'table' module.
 *
 * Implementes the global 'table' table, adding all functions to it to operate
 * on hashes.
 */

#include <stdio.h>

#include "config.h"
#include "lhash.h"
#include "lstate.h"
#include "vm.h"

static u32 lua_table_getn(LSTATE);
static u32 lua_table_maxn(LSTATE);
static u32 lua_table_insert(LSTATE);
static u32 lua_table_remove(LSTATE);
static LUAF(lua_table_getn);
static LUAF(lua_table_maxn);
static LUAF(lua_table_insert);
static LUAF(lua_table_remove);
static lhash_t lua_table;

INIT void lua_table_init() {
  lhash_init(&lua_table);

  REGISTER(&lua_table, "getn",   &lua_table_getn_f);
  REGISTER(&lua_table, "maxn",   &lua_table_maxn_f);
  REGISTER(&lua_table, "insert", &lua_table_insert_f);
  REGISTER(&lua_table, "remove", &lua_table_remove_f);

  lhash_set(&lua_globals, LSTR("table"), lv_table(&lua_table));
}

DESTROY void lua_table_destroy() {
  lhash_free(&lua_table);
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
