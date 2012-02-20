/**
 * @file lib/io.c
 * @brief Implementation of the io table of lua functions, having to do with
 *        input/output of files
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "error.h"
#include "lhash.h"
#include "lstate.h"
#include "lstring.h"
#include "meta.h"
#include "panic.h"
#include "vm.h"

/**
 * @brief Create a new luav for the given file
 *
 * Updates the global userdata metatable table accordingly.
 *
 * @param fptr the FILE* pointer
 */
#define lv_file(fptr) ({                                      \
          luav _lf = lv_userdata(fptr);                       \
          lhash_set(&userdata_meta, _lf, lv_table(&fd_meta)); \
          _lf;                                                \
        })

static luav str_r;
static luav str_w;
static luav str_open_failed;
static luav str_close_std;
static luav str_set;
static luav str_cur;
static luav str_end;
static luav str_file;
static luav str_closed_file;
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
static u32 lua_io_tmpfile(LSTATE);
static u32 lua_io_seek(LSTATE);
static u32 lua_io_read(LSTATE);
static u32 lua_io_output(LSTATE);
static u32 lua_io_type(LSTATE);

static LUAF(lua_io_close);
static LUAF(lua_io_flush);
static LUAF(lua_io_input);
static LUAF(lua_io_lines);
static LUAF(lua_io_open);
static LUAF(lua_io_write);
static LUAF(lua_io_tmpfile);
static LUAF(lua_io_seek);
static LUAF(lua_io_read);
static LUAF(lua_io_output);
static LUAF(lua_io_type);

INIT static void lua_io_init() {
  str_r = LSTR("r");
  str_w = LSTR("w");
  str_set = LSTR("set");
  str_cur = LSTR("cur");
  str_end = LSTR("end");
  str_file = LSTR("file");
  str_closed_file = LSTR("closed file");
  str_open_failed = LSTR("Error opening file");
  str_close_std = LSTR("cannot close standard file");
  default_out = stdout;
  default_in  = stdin;

  lhash_init(&fd_meta);
  lhash_set(&fd_meta, META_METATABLE, LUAV_TRUE);
  lhash_set(&fd_meta, META_INDEX, lv_table(&fd_meta));
  lhash_set(&fd_meta, LSTR("write"), lv_function(&lua_io_write_f));
  lhash_set(&fd_meta, LSTR("close"), lv_function(&lua_io_close_f));
  lhash_set(&fd_meta, LSTR("seek"),  lv_function(&lua_io_seek_f));
  lhash_set(&fd_meta, LSTR("read"),  lv_function(&lua_io_read_f));

  lhash_init(&lua_io);
  REGISTER(&lua_io, "close",   &lua_io_close_f);
  REGISTER(&lua_io, "flush",   &lua_io_flush_f);
  REGISTER(&lua_io, "input",   &lua_io_input_f);
  REGISTER(&lua_io, "lines",   &lua_io_lines_f);
  REGISTER(&lua_io, "open",    &lua_io_open_f);
  REGISTER(&lua_io, "write",   &lua_io_write_f);
  REGISTER(&lua_io, "tmpfile", &lua_io_tmpfile_f);
  REGISTER(&lua_io, "output",  &lua_io_output_f);
  REGISTER(&lua_io, "type",    &lua_io_type_f);
  lhash_set(&lua_io, LSTR("stdin"), lv_file(default_in));
  lhash_set(&lua_io, LSTR("stdout"), lv_file(default_out));

  lhash_set(&lua_globals, LSTR("io"), lv_table(&lua_io));
}

DESTROY static void lua_io_destroy() {}

/**
 * @brief Closes an input file, defaulting to stdout
 */
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

/**
 * @brief Flushes a file, defaulting to stdout
 */
static u32 lua_io_flush(LSTATE) {
  FILE *file = default_out;
  if (argc > 0)
    file = (FILE*) lstate_getuserdata(0);
  fflush(file);
  lstate_return1(LUAV_TRUE);
}

/**
 * @brief Opens a file for input
 *
 * If 0 parameters, the default input is returned.
 * If a filename paramter, this file is opened.
 * If a userdata parameter, this file becomes the default input.
 */
static u32 lua_io_input(LSTATE) {
  if (argc > 0) {
    luav arg = lstate_getval(0);
    switch (lv_gettype(arg)) {
      case LSTRING: {
        lstring_t *filename = lstate_getstring(0);
        FILE *file = fopen(filename->data, lv_caststring(str_r, 0)->data);
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
  /* TODO: implement */
  return 0;
}

/**
 * @brief Opens a file in a given mode
 *
 * @param filename the file to open
 * @param mode the mode in which to open the file
 */
static u32 lua_io_open(LSTATE) {
  lstring_t *filename = lstate_getstring(0);
  lstring_t *mode = lv_getptr(str_r);
  if (argc > 1) {
    mode = lstate_getstring(1);
  }

  FILE *f = fopen(filename->data, mode->data);
  if (f == NULL) {
    lstate_return(LUAV_NIL, 0);
    lstate_return(str_open_failed, 1);
    lstate_return(lv_number(errno), 2);
    return 3;
  }
  lstate_return1(lv_file(f));
}

/**
 * @brief Write to a file
 *
 * Defaults to the defaault output
 */
static u32 lua_io_write(LSTATE) {
  FILE *f = default_out;
  lstring_t *str;
  u32 i = 0;

  if (argc > 0) {
    luav tmp = lstate_getval(0);
    if (lv_isuserdata(tmp)) {
      f = (FILE*) lv_getptr(tmp);
      i++;
    }
  }

  for (; i < argc; i++) {
    luav value = lstate_getval(i);

    switch (lv_gettype(value)) {
      case LSTRING:
        str = lv_caststring(value, i);
        fprintf(f, "%.*s", (int) str->length, str->data);
        break;

      case LNUMBER:
        fprintf(f, LUA_NUMBER_FMT, lv_castnumber(value, i));
        break;

      default:
        err_badtype(i, LSTRING, lv_gettype(value));
    }
  }

  lstate_return1(LUAV_TRUE);
}

/**
 * @brief Opens a temporary file which is automatically removed later.
 */
static u32 lua_io_tmpfile(LSTATE) {
  FILE *f = tmpfile();
  if (f == NULL) {
    err_rawstr("Couldn't open temporary file", TRUE);
  }
  lstate_return1(lv_file(f));
}

/**
 * @brief Seeks a file to a specified position
 */
static u32 lua_io_seek(LSTATE) {
  FILE *f = (FILE*) lstate_getuserdata(0);
  i32 offset = 0;
  int whence = SEEK_CUR;
  if (argc > 1) {
    luav whence_str = lstate_getval(1);
    if (whence_str == str_set) {
      whence = SEEK_SET;
    } else if (whence_str == str_end) {
      whence = SEEK_END;
    }
  }
  if (argc > 2) {
    offset = (i32) lstate_getnumber(2);
  }

  if (fseek(f, offset, whence) != 0) {
    lstate_return(LUAV_NIL, 0);
    lstate_return(lv_string(lstr_literal(strerror(errno))), 1);
    return 2;
  }
  lstate_return1(lv_number((double) ftell(f)));
}

/**
 * @brief Seeks a file to a specified position
 */
static u32 lua_io_read(LSTATE) {
  panic("not implemented");
}

/**
 * @brief Fetches the default output, possibly setting it in the process
 */
static u32 lua_io_output(LSTATE) {
  if (argc > 0) {
    luav val = lstate_getval(0);
    if (lv_isuserdata(val)) {
      default_out = lstate_getuserdata(0);
    } else {
      lstring_t *path = lv_caststring(val, 0);
      FILE *f = fopen(path->data, "w");
      if (f == NULL) {
        err_rawstr("Couldn't open file", TRUE);
      }
      default_out = f;
    }
  }
  lstate_return1(lv_file(default_out));
}

/**
 * @brief Tests whether the input is a file handle or not
 */
static u32 lua_io_type(LSTATE) {
  luav val = lstate_getval(0);
  if (lv_isuserdata(val)) {
    FILE *f = lv_getptr(val);
    if (feof(f)) {
      lstate_return1(str_closed_file);
    }
    lstate_return1(str_file);
  }
  lstate_return1(LUAV_NIL);
}
