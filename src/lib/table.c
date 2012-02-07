#include <stdio.h>

#include "config.h"
#include "lhash.h"
#include "lstate.h"
#include "vm.h"

static u32 lua_table_getn(LSTATE);
static u32 lua_table_maxn(LSTATE);
static LUAF(lua_table_getn);
static LUAF(lua_table_maxn);
static lhash_t lua_table;

INIT void lua_table_init() {
  lhash_init(&lua_table);

  REGISTER(&lua_table, "getn", &lua_table_getn_f);
  REGISTER(&lua_table, "maxn", &lua_table_maxn_f);

  lhash_set(&lua_globals, LSTR("table"), lv_table(&lua_table));
}

DESTROY void lua_table_destroy() {
  lhash_free(&lua_table);
}

static u32 lua_table_getn(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  luav key, value;
  lhash_next(table, LUAV_NIL, &key, &value);
  lstate_return1(lv_number(table->length));
}

static u32 lua_table_maxn(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  lstate_return1(lv_number(lhash_maxn(table)));
}
