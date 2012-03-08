#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "lhash.h"
#include "lstate.h"
#include "luav.h"
#include "vm.h"

#define MAX_FORMAT 20

/**
 * @brief Resize a string if necessary so that the given size will fit in
 *        the string's capacity
 *
 * @param str the string to possibly resize
 * @param size the desired size of the string
 */
#define RESIZE(str, size)                     \
  if ((size) >= (str)->length) {              \
    str = lstr_realloc(str, str->length * 2); \
  }

/**
 * @brief Call snprintf() until it successfully fits entirely inside the given
 *        string
 *
 * @param str the string to write into
 * @param size the current size of the string
 * @param fmt the format and arguments to pass to snprintf()
 */
#define SNPRINTF(str, size, fmt...) {                                          \
    int tmp;                                                                   \
    while ((size_t) (tmp = snprintf(&str->data[size], str->length - size, fmt))\
                           + size >= str->length) {                            \
      str = lstr_realloc(str, (size_t) tmp + size + 1);                        \
    }                                                                          \
    size += (size_t) tmp;                                                      \
  }

/**
 * @brief Append one character onto a string
 *
 * @param str the string to append to
 * @param size the size of the string
 * @param c the character to add to the buffer
 */
#define APPEND(str, size, c) {  \
    RESIZE(str, size + 1);      \
    str->data[size++] = c;      \
  }

/**
 * @brief Fix beginning/end indices for a string to be real indices
 *
 * @param i the variable which holds the left indice
 * @param j the variable which holds the right indice
 * @param len the length of the string
 *
 * TODO: this has to be wrong somehow, find a better way?
 */
#define FIX_INDICES(i, j, len) {                              \
    if ((i < -len && j < -len) || (i > len && j > len)) {     \
      i = 0;                                                  \
      j = -1;                                                 \
    } else {                                                  \
      if (i < -len) {                                         \
        i = 0;                                                \
      } else if (i < 0) {                                     \
        i += len;                                             \
      } else if (i >= len) {                                  \
        i = len - 1;                                          \
      } else if (i > 0) {                                     \
        i--;                                                  \
      }                                                       \
      if (j < -len) {                                         \
        j = 0;                                                \
      } else if (j < 0) {                                     \
        j += len;                                             \
      } else if (j >= len) {                                  \
        j = len - 1;                                          \
      } else if (j > 0) {                                     \
        j--;                                                  \
      }                                                       \
    }                                                         \
  }

static luav str_empty;
static lhash_t *lua_string;
static u32 lua_string_format(LSTATE);
static u32 lua_string_rep(LSTATE);
static u32 lua_string_sub(LSTATE);
static u32 lua_string_len(LSTATE);
static u32 lua_string_lower(LSTATE);
static u32 lua_string_upper(LSTATE);
static u32 lua_string_reverse(LSTATE);
static u32 lua_string_byte(LSTATE);
static u32 lua_string_char(LSTATE);
static u32 lua_string_find(LSTATE);
static char errbuf[200];

INIT static void lua_string_init() {
  str_empty = lv_string(lstr_empty());

  lua_string = lhash_alloc();
  cfunc_register(lua_string, "format",  lua_string_format);
  cfunc_register(lua_string, "rep",     lua_string_rep);
  cfunc_register(lua_string, "sub",     lua_string_sub);
  cfunc_register(lua_string, "len",     lua_string_len);
  cfunc_register(lua_string, "lower",   lua_string_lower);
  cfunc_register(lua_string, "upper",   lua_string_upper);
  cfunc_register(lua_string, "reverse", lua_string_reverse);
  cfunc_register(lua_string, "byte",    lua_string_byte);
  cfunc_register(lua_string, "char",    lua_string_char);
  cfunc_register(lua_string, "find",    lua_string_find);

  lhash_set(lua_globals, LSTR("string"), lv_table(lua_string));
}

static u32 lua_string_format(LSTATE) {
  lstring_t *lfmt = lstate_getstring(0);
  if (lfmt->length == 0) { lstate_return1(str_empty); }
  size_t len = 0;
  lstring_t *newstr = lstr_alloc(LUAV_INIT_STRING);
  char *fmt = lfmt->data;
  u32 i, j, argi = 0;
  char buf[MAX_FORMAT];

  for (i = 0; i < lfmt->length; i++) {
    if (fmt[i] != '%') {
      APPEND(newstr, len, fmt[i]);
      continue;
    }

    /* Figure out what exactly is the format string, moving it into a separate
       buffer to pass to snprintf() later */
    u32  start = i;
    char *pct_start = &fmt[i++];
    if (fmt[i] == '-') i++;
    for (j = 0; j < 3 && isdigit(fmt[i]); j++) i++; /* skip the width */
    /* TODO: take out these two asserts */
    assert(!isdigit(fmt[i]));
    if (fmt[i] == '.') i++;                         /* skip period format */
    if (fmt[i] == '-') i++;
    for (j = 0; j < 3 && isdigit(fmt[i]); j++) i++; /* skip the precision */
    assert(!isdigit(fmt[i]));

    strncpy(buf, pct_start, i - start + 1);
    buf[i - start + 1] = 0;

    argi++;
    switch (fmt[i]) {
      case '%':
        APPEND(newstr, len, '%');
        argi--;
        break;
      case 'c':
        SNPRINTF(newstr, len, buf, (char) lstate_getnumber(argi));
        break;

      case 'i':
      case 'd':
        SNPRINTF(newstr, len, buf, (int) lstate_getnumber(argi));
        break;

      case 'o':
      case 'u':
      case 'x':
      case 'X': {
        /* Use the generic PRIu64 macro to print a 64-bit argument, but change
           the last character to whatever format we specified. */
        u32 end = i - start;
        buf[end + sizeof(PRIu64) - 1] = 0;
        memcpy(&buf[end], PRIu64, sizeof(PRIu64) - 1);
        buf[end + sizeof(PRIu64) - 2] = fmt[i];
        SNPRINTF(newstr, len, buf, (i64) lstate_getnumber(argi));
        break;
      }

      case 'e':
      case 'E':
      case 'f':
      case 'g':
      case 'G':
        SNPRINTF(newstr, len, buf, lstate_getnumber(argi));
        break;

      case 'q':
      case 's': {
        luav arg = lstate_getval(argi);
        lstring_t *str = lv_caststring(arg, argi);
        if (fmt[i] == 's') {
          SNPRINTF(newstr, len, buf, str->data);
          break;
        }
        APPEND(newstr, len, '"');
        for (j = 0; j < str->length; j++) {
          /* Make sure we escape all escape sequences */
          switch (str->data[j]) {
            case 0:
              APPEND(newstr, len, '\\');
              APPEND(newstr, len, '0');
              APPEND(newstr, len, '0');
              APPEND(newstr, len, '0');
              break;

            case '"':
              APPEND(newstr, len, '\\');
              APPEND(newstr, len, '"');
              break;

            case '\n':
              APPEND(newstr, len, '\\');
              APPEND(newstr, len, '\n');
              break;

            case '\\':
              APPEND(newstr, len, '\\');
              APPEND(newstr, len, '\\');
              break;

            default:
              APPEND(newstr, len, str->data[j]);
              break;
          }
        }
        APPEND(newstr, len, '"');
        break;
      }

      default:
        sprintf(errbuf, "invalid option '%%%c' to 'format'", fmt[i]);
        err_rawstr(errbuf, TRUE);
    }
  }

  APPEND(newstr, len, 0);
  newstr->length = len - 1;

  lstate_return1(lv_string(lstr_add(newstr)));
}

static u32 lua_string_rep(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  size_t n = (size_t) lstate_getnumber(1);
  /* Avoid malloc if we can */
  if (n == 0 || str->length == 0) {
    lstate_return1(str_empty);
  }
  size_t len = n * str->length;

  lstring_t *newstr = lstr_alloc(len);
  char *ptr = newstr->data;
  while (n-- > 0) {
    memcpy(ptr, str->data, str->length);
    ptr += str->length;
  }
  newstr->data[len] = 0;

  lstate_return1(lv_string(lstr_add(newstr)));
}

static u32 lua_string_sub(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  ssize_t strlen = (ssize_t) str->length;
  ssize_t start = (ssize_t) lstate_getnumber(1);
  ssize_t end;
  if (argc < 3) {
    end = strlen;
  } else {
    end = (ssize_t) lstate_getnumber(2);
  }

  FIX_INDICES(start, end, strlen);

  if (end == 0 || end < start) {
    lstate_return1(str_empty);
  }

  size_t len = (size_t) (end - start + 1);
  lstring_t *newstr = lstr_alloc(len);
  memcpy(newstr->data, str->data + start, len);
  newstr->data[len] = 0;

  lstate_return1(lv_string(lstr_add(newstr)));
}

static u32 lua_string_len(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  lstate_return1(lv_number((double) str->length));
}

static u32 lua_string_lower(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  if (str->length == 0) { lstate_return1(str_empty); }
  size_t i;
  lstring_t *newstr = lstr_alloc(str->length);
  for (i = 0; i < str->length; i++) {
    newstr->data[i] = (char) tolower(str->data[i]);
  }
  newstr->data[i] = 0;
  lstate_return1(lv_string(lstr_add(newstr)));
}

static u32 lua_string_upper(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  if (str->length == 0) { lstate_return1(str_empty); }
  size_t i;
  lstring_t *newstr = lstr_alloc(str->length);
  for (i = 0; i < str->length; i++) {
    newstr->data[i] = (char) toupper(str->data[i]);
  }
  newstr->data[i] = 0;
  lstate_return1(lv_string(lstr_add(newstr)));
}

static u32 lua_string_reverse(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  if (str->length == 0) { lstate_return1(str_empty); }
  size_t i;
  lstring_t *newstr = lstr_alloc(str->length);
  for (i = 0; i < str->length; i++) {
    newstr->data[i] = str->data[str->length - i - 1];
  }
  newstr->data[i] = 0;
  lstate_return1(lv_string(lstr_add(newstr)));
}

static u32 lua_string_byte(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  ssize_t i, j, len = (ssize_t) str->length;
  i = argc < 2 || lstate_getval(1) == LUAV_NIL ?
        1 : (ssize_t) lstate_getnumber(1);
  j = argc < 3 || lstate_getval(2) == LUAV_NIL ?
        i : (ssize_t) lstate_getnumber(2);

  FIX_INDICES(i, j, len);

  u32 k;
  if (j < i) { return 0; }
  for (k = 0; k < retc && k <= (u32) (j - i); k++) {
    /* All bytes are considered unsigned, so we need to cast from char to u8 */
    lstate_return(lv_number((u8) str->data[(u32) i + k]), k);
  }
  return (u32) k;
}

static u32 lua_string_char(LSTATE) {
  if (argc == 0) {
    lstate_return1(str_empty);
  }
  lstring_t *str = lstr_alloc(argc);
  u32 i;
  for (i = 0; i < argc; i++) {
    str->data[i] = (char) lstate_getnumber(i);
  }
  str->data[i] = 0;
  lstate_return1(lv_string(lstr_add(str)));
}

static u32 lua_string_find(LSTATE) {
  lstring_t *s = lstate_getstring(0);
  lstring_t *pat = lstate_getstring(1);
  i32 init = argc > 2 ? (i32) lstate_getnumber(2) : 1;

  if (init < (i32) -s->length) {
    init = (i32) s->length;
  } else if (init < 0) {
    init += (i32) s->length;
  } else if (init == 0) {
    init = 1;
  }

  /* TODO: this is supposed to use regexes... */
  char *ptr = strstr(s->data + (init - 1), pat->data);

  if (ptr == NULL) {
    lstate_return1(LUAV_NIL);
  }

  size_t start = ((size_t) ptr - (size_t) s->data) + 1;
  size_t end = start + pat->length - 1;
  lstate_return(lv_number((double) start), 0);
  lstate_return(lv_number((double) end), 1);
  return 2;
}
