#include <stdio.h>

#include "config.h"
#include "lhash.h"
#include "lstate.h"
#include "vm.h"

static u32 lua_table_getn(LSTATE);
static LUAF(lua_table_getn);
static lhash_t lua_table;

INIT void lua_table_init() {
  lhash_init(&lua_table);

  REGISTER(&lua_table, "getn", &lua_table_getn_f);

  lhash_set(&lua_globals, LSTR("table"), lv_table(&lua_table));
}

DESTROY void lua_table_destroy() {
  lhash_free(&lua_table);
}

static u32 lua_table_getn(LSTATE) {
  lhash_t *table = lstate_gettable(0);
  printf("%p %d\n", table, table->length);
  luav key, value;
  lhash_next(table, LUAV_NIL, &key, &value);
  printf("0x%016llx\n", key);
  lstate_return1(lv_number(table->length));
}