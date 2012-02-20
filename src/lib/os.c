/**
 * @file lib/os.c
 * @brief Implementation of the OS table in lua
 */

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gc.h"
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
static luav str_collate;
static luav str_ctype;
static luav str_monetary;
static luav str_numeric;
static luav str_time;

static lhash_t lua_os;
static u32 lua_os_clock(LSTATE);
static u32 lua_os_exit(LSTATE);
static u32 lua_os_execute(LSTATE);
static u32 lua_os_getenv(LSTATE);
static u32 lua_os_date(LSTATE);
static u32 lua_os_setlocale(LSTATE);
static u32 lua_os_tmpname(LSTATE);
static u32 lua_os_remove(LSTATE);
static u32 lua_os_rename(LSTATE);
static LUAF(lua_os_clock);
static LUAF(lua_os_exit);
static LUAF(lua_os_execute);
static LUAF(lua_os_getenv);
static LUAF(lua_os_date);
static LUAF(lua_os_setlocale);
static LUAF(lua_os_tmpname);
static LUAF(lua_os_remove);
static LUAF(lua_os_rename);

INIT static void lua_os_init() {
  str_sec      = LSTR("sec");
  str_min      = LSTR("min");
  str_hour     = LSTR("hour");
  str_day      = LSTR("day");
  str_month    = LSTR("month");
  str_year     = LSTR("year");
  str_wday     = LSTR("wday");
  str_yday     = LSTR("yday");
  str_isdst    = LSTR("isdst");
  str_collate  = LSTR("collate");
  str_ctype    = LSTR("ctype");
  str_monetary = LSTR("monetary");
  str_numeric  = LSTR("numeric");
  str_time     = LSTR("time");

  lhash_init(&lua_os);
  REGISTER(&lua_os, "clock",     &lua_os_clock_f);
  REGISTER(&lua_os, "exit",      &lua_os_exit_f);
  REGISTER(&lua_os, "execute",   &lua_os_execute_f);
  REGISTER(&lua_os, "getenv",    &lua_os_getenv_f);
  REGISTER(&lua_os, "date",      &lua_os_date_f);
  REGISTER(&lua_os, "setlocale", &lua_os_setlocale_f);
  REGISTER(&lua_os, "tmpname",   &lua_os_tmpname_f);
  REGISTER(&lua_os, "remove",    &lua_os_remove_f);
  REGISTER(&lua_os, "rename",    &lua_os_rename_f);

  lhash_set(&lua_globals, LSTR("os"), lv_table(&lua_os));
}

DESTROY static void lua_os_destroy() {}

static u32 lua_os_clock(LSTATE) {
  clock_t c = clock();
  lstate_return1(lv_number((double) c / CLOCKS_PER_SEC));
}

static u32 lua_os_exit(LSTATE) {
  exit((int) lstate_getnumber(0));
}

static u32 lua_os_execute(LSTATE) {
  if (argc == 0 || lstate_getval(0) == LUAV_NIL) {
    lstate_return1(lv_number(1));
  }
  lstring_t *str = lstate_getstring(0);
  lstate_return1(lv_number(system(str->data)));
}

static u32 lua_os_getenv(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  char *value = getenv(str->data);

  if (value == NULL) {
    lstate_return1(LUAV_NIL);
  }

  lstate_return1(LSTR(value));
}

static u32 lua_os_date(LSTATE) {
  luav first = argc > 0 ? lstate_getval(0) : LUAV_NIL;
  char *format;
  if (first == LUAV_NIL) {
    format = "%c";
  } else {
    format = lv_caststring(first, 0)->data;
  }

  time_t t = argc <= 1 ? time(NULL) : (time_t) lstate_getnumber(1);
  struct tm *stm;
  if (format[0] == '!') {
    stm = gmtime(&t);
    format++;
  } else {
    stm = localtime(&t);
  }
  xassert(stm != NULL);

  if (strcmp("*t", format) == 0) {
    lhash_t *hash = gc_alloc(sizeof(lhash_t));
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

    lstate_return1(lv_table(hash));
  }

  size_t cap = LUAV_INIT_STRING;
  lstring_t *str = lstr_alloc(cap);

  while (strftime(str->data, cap - 1, format, stm) == 0) {
    cap *= 2;
    str = lstr_realloc(str, cap);
  }

  str->length = strlen(str->data);
  lstate_return1(lv_string(str));
}

static u32 lua_os_setlocale(LSTATE) {
  lstring_t *locale = lstate_getstring(0);
  luav lcategory = LUAV_NIL;
  if (argc > 1) {
    lcategory = lstate_getval(1);
  }
  int category = LC_ALL;
  if (lcategory == str_collate) {
    category = LC_COLLATE;
  } else if (lcategory == str_ctype) {
    category = LC_CTYPE;
  } else if (lcategory == str_monetary) {
    category = LC_MONETARY;
  } else if (lcategory == str_numeric) {
    category = LC_NUMERIC;
  } else if (lcategory == str_time) {
    category = LC_TIME;
  }

  char *ret = setlocale(category, locale->data);
  if (ret == NULL) {
    lstate_return1(LUAV_NIL);
  }
  lstate_return1(LSTR(ret));
}

/**
 * @brief Generates a name for a temporary file
 */
static u32 lua_os_tmpname(LSTATE) {
  char buf[L_tmpnam];
  strncpy(buf, "/tmp/joule.XXXXXXXX", sizeof(buf));
  int fd = mkstemp(buf);
  if (fd == -1) {
    lstate_return1(LUAV_NIL);
  }
  close(fd);
  lstate_return1(lv_string(lstr_literal(buf)));
}

/**
 * @brief Removes a file or directory (if it's empty)
 */
static u32 lua_os_remove(LSTATE) {
  lstring_t *fname = lstate_getstring(0);
  if (remove(fname->data) == 0) {
    lstate_return1(LUAV_TRUE);
  }
  int err = errno;

  lstate_return(LUAV_NIL, 0);
  lstate_return(lv_string(lstr_literal(strerror(err))), 1);
  return 2;
}

/**
 * @brief Removes a file or directory (if it's empty)
 */
static u32 lua_os_rename(LSTATE) {
  lstring_t *f1 = lstate_getstring(0);
  lstring_t *f2 = lstate_getstring(1);
  if (rename(f1->data, f2->data) == 0) {
    lstate_return1(LUAV_TRUE);
  }
  int err = errno;

  lstate_return(LUAV_NIL, 0);
  lstate_return(lv_string(lstr_literal(strerror(err))), 1);
  return 2;
}
