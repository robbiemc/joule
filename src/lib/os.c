#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "panic.h"
#include "vm.h"

static luav str_sec;
static luav str_min;
static luav str_hour;
static luav str_day;
static luav str_month;
static luav str_year;
static luav str_wday;
static luav str_yday;
static luav str_isdst;

static lhash_t lua_os;
static luav lua_os_clock(void);
static luav lua_os_exit(luav status);
static luav lua_os_execute(luav cmd);
static luav lua_os_getenv(luav var);
static luav lua_os_date(luav fmt, luav time);
static LUAF_0ARG(lua_os_clock);
static LUAF_1ARG(lua_os_exit);
static LUAF_1ARG(lua_os_execute);
static LUAF_1ARG(lua_os_getenv);
static LUAF_2ARG(lua_os_date);

INIT static void lua_os_init() {
  str_sec   = LSTR("sec");
  str_min   = LSTR("min");
  str_hour  = LSTR("hour");
  str_day   = LSTR("day");
  str_month = LSTR("month");
  str_year  = LSTR("year");
  str_wday  = LSTR("wday");
  str_yday  = LSTR("yday");
  str_isdst = LSTR("isdst");

  lhash_init(&lua_os);
  lhash_set(&lua_os, LSTR("clock"),   lv_function(&lua_os_clock_f));
  lhash_set(&lua_os, LSTR("exit"),    lv_function(&lua_os_exit_f));
  lhash_set(&lua_os, LSTR("execute"), lv_function(&lua_os_execute_f));
  lhash_set(&lua_os, LSTR("getenv"),  lv_function(&lua_os_getenv_f));
  lhash_set(&lua_os, LSTR("date"),    lv_function(&lua_os_date_f));

  lhash_set(&lua_globals, LSTR("os"), lv_table(&lua_os));
}

DESTROY static void lua_os_destroy() {
  lhash_free(&lua_os);
}

static luav lua_os_clock() {
  clock_t c = clock();
  return lv_number((double) c / CLOCKS_PER_SEC);
}

static luav lua_os_exit(luav status) {
  exit((int) lv_getnumber(lv_tonumber(status, 10)));
}

static luav lua_os_execute(luav cmd) {
  switch (lv_gettype(cmd)) {
    case LNIL:
    case LNUMBER: return lv_number(1);
    case LSTRING: break;
    default:      panic("bad type in execute: %d", lv_gettype(cmd));
  }

  lstring_t *str = lstr_get(lv_getstring(cmd));
  return lv_number(system(str->ptr));
}

static luav lua_os_getenv(luav var) {
  lstring_t *str = lstr_get(lv_getstring(var));
  char *value = getenv(str->ptr);

  if (value == NULL) {
    return LUAV_NIL;
  }

  return lv_string(lstr_add(value, strlen(value), FALSE));
}

static luav lua_os_date(luav fmt, luav _time) {
  char *format;
  if (fmt == LUAV_NIL) {
    format = "%c";
  } else {
    format = lstr_get(lv_getstring(fmt))->ptr;
  }

  time_t t = _time == LUAV_NIL ? time(NULL) : (time_t) lv_getnumber(_time);
  struct tm *stm;
  if (format[0] == '!') {
    stm = gmtime(&t);
    format++;
  } else {
    stm = localtime(&t);
  }

  if (stm == NULL) {
    return LUAV_NIL;
  } else if (strcmp("*t", format) == 0) {
    lhash_t *hash = xmalloc(sizeof(lhash_t));
    lhash_init(hash);

    lhash_set(hash, str_sec,    lv_number(stm->tm_sec));
    lhash_set(hash, str_min,    lv_number(stm->tm_min));
    lhash_set(hash, str_hour,   lv_number(stm->tm_hour));
    lhash_set(hash, str_day,    lv_number(stm->tm_mday));
    lhash_set(hash, str_month,  lv_number(stm->tm_mon + 1));
    lhash_set(hash, str_year,   lv_number(stm->tm_year + 1900));
    lhash_set(hash, str_wday,   lv_number(stm->tm_wday + 1));
    lhash_set(hash, str_yday,   lv_number(stm->tm_yday + 1));
    lhash_set(hash, str_isdst,  lv_bool((u8) stm->tm_isdst));

    return lv_table(hash);
  } else {
    size_t cap = LUAV_INIT_STRING;
    char *str = xmalloc(cap);

    while (strftime(str, cap, format, stm) >= cap) {
      cap *= 2;
      str = xrealloc(str, cap);
    }

    size_t len = strlen(str);
    if (len == cap) { str = xrealloc(str, cap + 1); }
    str[len] = 0;

    return lv_string(lstr_add(str, len, TRUE));
  }
}
