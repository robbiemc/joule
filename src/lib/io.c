#include <stdio.h>

#include "error.h"
#include "lhash.h"
#include "lstate.h"
#include "lstring.h"
#include "meta.h"
#include "vm.h"

#define lv_file(fptr) ({                                      \
          luav _lf = lv_userdata(fptr);                       \
          lhash_set(&userdata_meta, _lf, lv_table(&fd_meta)); \
          _lf;                                                \
        })
#define lopen(fname, mode) ({                                 \
          FILE *_f = fopen((fname), (mode));                  \
          if (_f == NULL) {                                   \
            lstate_return(LUAV_NIL, 0);                       \
            lstate_return(str_open_failed, 1);                \
            return 2;                                         \
          }                                                   \
          lv_file(_f);                                        \
        })

static luav str_r;
static luav str_w;
static luav str_open_failed;
static luav str_close_std;
static lhash_t fd_meta;
static FILE *default_out;
static FILE *default_in;

static lhash_t lua_io;
static u32 lua_io_close(LSTATE);
static u32 lua_io_flush(LSTATE);
static u32 lua_io_input(LSTATE);
static u32 lua_io_lines(LSTATE);
static u32 lua_io_open(LSTATE);
static u32 lua_io_write(LSTATE);
static u32 lua_io_meta_index(LSTATE);

static LUAF(lua_io_close);
static LUAF(lua_io_flush);
static LUAF(lua_io_input);
static LUAF(lua_io_lines);
static LUAF(lua_io_open);
static LUAF(lua_io_write);
static LUAF(lua_io_meta_index);

INIT static void lua_io_init() {
  str_r = LSTR("r");
  str_w = LSTR("w");
  str_open_failed = LSTR("Error opening file");
  str_close_std = LSTR("cannot close standard file");
  default_out = stdout;
  default_in  = stdin;

  lhash_init(&fd_meta);
  lhash_set(&fd_meta, meta_strings[META_METATABLE], LUAV_TRUE);
  lhash_set(&fd_meta, meta_strings[META_INDEX],
                      lv_function(&lua_io_meta_index_f));

  lhash_init(&lua_io);
  REGISTER(&lua_io, "close",  &lua_io_close_f);
  REGISTER(&lua_io, "flush",  &lua_io_flush_f);
  REGISTER(&lua_io, "input",  &lua_io_input_f);
  REGISTER(&lua_io, "lines",  &lua_io_lines_f);
  REGISTER(&lua_io, "open",   &lua_io_open_f);
  REGISTER(&lua_io, "write",  &lua_io_write_f);

  lhash_set(&lua_globals, LSTR("io"), lv_table(&lua_io));
}

DESTROY static void lua_io_destroy() {
  lhash_free(&lua_io);
}

static u32 lua_io_close(LSTATE) {
  FILE *file = default_out;
  if (argc > 0)
    file = (FILE*) lstate_getuserdata(0);
  if (file == stdout || file == stdin) {
    lstate_return(LUAV_NIL, 0);
    lstate_return(str_close_std, 1);
    return 2;
  }
  fclose(file);
  lstate_return1(LUAV_TRUE);
}

static u32 lua_io_flush(LSTATE) {
  FILE *file = default_out;
  if (argc > 0)
    file = (FILE*) lstate_getuserdata(0);
  fflush(file);
  lstate_return1(LUAV_TRUE);
}

static u32 lua_io_input(LSTATE) {
  if (argc > 0) {
    luav arg = lstate_getval(0);
    switch (lv_gettype(arg)) {
      case LSTRING: {
        lstring_t *filename = lstate_getstring(0);
        FILE *file = fopen(filename->ptr, lv_caststring(str_r, 0)->ptr);
        if (file == NULL)
          err_rawstr("Error opening file in io.input", TRUE);
        default_in = file;
        lstate_return1(lv_file(file));
      }
      case LUSERDATA:
        default_in = (FILE*) lv_getuserdata(arg, 0);
        break;
      default:
        err_str(1, "Invalid argument type. Expcected userdata or string.");
    }
  }
  lstate_return1(lv_userdata(default_in));
}

static u32 lua_io_lines(LSTATE) {
  return 0;
}

static u32 lua_io_open(LSTATE) {
  lstring_t *filename = lstate_getstring(0);
  lstring_t *mode = lv_caststring(str_r, 2);
  if (argc > 1) {
    mode = lstate_getstring(1);
  }

  luav lfile = lopen(filename->ptr, mode->ptr);
  lstate_return1(lfile);
}

static u32 lua_io_meta_index(LSTATE) {
  printf("Index was called!\n");
  return 0;
}

static u32 lua_io_write(LSTATE) {
  lstring_t *str;
  u32 i;

  for (i = 0; i < argc; i++) {
    luav value = lstate_getval(i);

    switch (lv_gettype(value)) {
      case LSTRING:
        str = lv_caststring(value, i);
        printf("%.*s", (int) str->length, str->ptr);
        break;

      case LNUMBER:
        printf(LUA_NUMBER_FMT, lv_castnumber(value, i));
        break;

      default:
        err_badtype(i, LSTRING, lv_gettype(value));
    }
  }

  lstate_return1(LUAV_TRUE);
}
