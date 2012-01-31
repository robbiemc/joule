#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lhash.h"
#include "lstate.h"
#include "luav.h"
#include "panic.h"
#include "vm.h"

#define MAX_FORMAT 20

/**
 * @brief Resize a buffer if necessary so that the given size will fit in
 *        the buffer's capacity
 *
 * @param buf the buffer to possibly resize
 * @param size the desired size of the buffer
 * @param cap the current capacity of the buffer
 */
#define RESIZE(buf, size, cap)    \
  if ((size) >= cap) {            \
    cap *= 2;                     \
    buf = xrealloc(buf, cap * 2); \
  }

/**
 * @brief Call snprintf() until it successfully fits entirely inside the given
 *        buffer
 *
 * @param buf the buffer to write into
 * @param size the current size of the buffer
 * @param cap the current capacity of the buffer
 * @param fmt the format and arguments to pass to snprintf()
 */
#define SNPRINTF(buf, size, cap, fmt...) {                                   \
    int tmp;                                                                 \
    while ((size_t) (tmp = snprintf(&buf[size], cap - size, fmt)) + size >= cap) {  \
      cap *= 2;                                                              \
      buf = xrealloc(buf, cap * 2);                                          \
    }                                                                        \
    len += (size_t) tmp;                                                     \
  }

/**
 * @brief Append one character into a resizable buffer
 *
 * @param buf the buffer to write into
 * @param size the size of the buffer
 * @param cap the capacity of the buffer
 * @param c the character to add to the buffer
 */
#define APPEND(buf, size, cap, c) {   \
    RESIZE(buf, size + 1, cap);       \
    buf[size++] = c;                  \
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
static lhash_t lua_string;
static u32 lua_string_format(LSTATE);
static u32 lua_string_rep(LSTATE);
static u32 lua_string_sub(LSTATE);
static u32 lua_string_len(LSTATE);
static u32 lua_string_lower(LSTATE);
static u32 lua_string_upper(LSTATE);
static u32 lua_string_reverse(LSTATE);
static u32 lua_string_byte(LSTATE);
static u32 lua_string_char(LSTATE);

static LUAF(lua_string_format);
static LUAF(lua_string_rep);
static LUAF(lua_string_sub);
static LUAF(lua_string_len);
static LUAF(lua_string_upper);
static LUAF(lua_string_lower);
static LUAF(lua_string_reverse);
static LUAF(lua_string_byte);
static LUAF(lua_string_char);

INIT static void lua_string_init() {
  str_empty = LSTR("");

  lhash_init(&lua_string);
  REGISTER(&lua_string, "format",  &lua_string_format_f);
  REGISTER(&lua_string, "rep",     &lua_string_rep_f);
  REGISTER(&lua_string, "sub",     &lua_string_sub_f);
  REGISTER(&lua_string, "len",     &lua_string_len_f);
  REGISTER(&lua_string, "lower",   &lua_string_lower_f);
  REGISTER(&lua_string, "upper",   &lua_string_upper_f);
  REGISTER(&lua_string, "reverse", &lua_string_reverse_f);
  REGISTER(&lua_string, "byte",    &lua_string_byte_f);
  REGISTER(&lua_string, "char",    &lua_string_char_f);

  lhash_set(&lua_globals, LSTR("string"), lv_table(&lua_string));
}

DESTROY static void lua_string_destroy() {
  lhash_free(&lua_string);
}

static u32 lua_string_format(LSTATE) {
  lstring_t *lfmt = lstate_getstring(0);
  if (lfmt->length == 0) { lstate_return1(str_empty); }
  size_t len = 0, cap = LUAV_INIT_STRING;
  char *newstr = xmalloc(cap);
  char *fmt = lfmt->ptr;
  u32 i, j, argi = 0;
  char buf[MAX_FORMAT];

  for (i = 0; i < lfmt->length; i++) {
    if (fmt[i] != '%') {
      APPEND(newstr, len, cap, fmt[i]);
      continue;
    }

    /* Figure out what exactly is the format string, moving it into a separate
       buffer to pass to snprintf() later */
    u32  start = i;
    char *pct_start = &fmt[i++];
    if (fmt[i] == '-') i++;
    for (j = 0; j < 3 && isdigit(fmt[i]); j++) i++; /* skip the width */
    assert(!isdigit(fmt[i]));
    if (fmt[i] == '.') i++;                         /* skip period format */
    if (fmt[i] == '-') i++;
    for (j = 0; j < 3 && isdigit(fmt[i]); j++) i++; /* skip the precision */
    assert(!isdigit(fmt[i]));

    strncpy(buf, pct_start, i - start + 1);
    buf[i - start + 1] = 0;

    argi++;
    assert(argi < argc);
    switch (fmt[i]) {
      case '%':
        APPEND(newstr, len, cap, '%');
        argi--;
        break;
      case 'c':
        SNPRINTF(newstr, len, cap, buf, (char) lstate_getnumber(argi));
        break;

      case 'i':
      case 'd':
        SNPRINTF(newstr, len, cap, buf, (int) lstate_getnumber(argi));
        break;

      case 'o':
      case 'u':
      case 'x':
      case 'X': {
        /* Make sure the format has an 'l' in front so it actually uses the
           longer forms to print out more bits */
        u32 end = i - start;
        buf[end + 1] = buf[end];
        buf[end + 2] = 0;
        buf[end] = 'l';
        SNPRINTF(newstr, len, cap, buf, (size_t) lstate_getnumber(argi));
        break;
      }

      case 'e':
      case 'E':
      case 'f':
      case 'g':
      case 'G':
        SNPRINTF(newstr, len, cap, buf, lstate_getnumber(argi));
        break;

      case 'q':
      case 's': {
        luav arg = lstate_getval(argi);
        lstring_t *str;
        switch (lv_gettype(arg)) {
          /* Numbers are coerced to strings, but nothing else is... */
          case LNUMBER:
            if (fmt[i] == 'q') APPEND(newstr, len, cap, '"');
            SNPRINTF(newstr, len, cap, LUA_NUMBER_FMT,
                     lv_castnumber(arg, argi));
            if (fmt[i] == 'q') APPEND(newstr, len, cap, '"');
            break;

          case LSTRING:
            str = lv_caststring(arg, argi);
            if (fmt[i] == 's') {
              SNPRINTF(newstr, len, cap, buf, str->ptr);
              break;
            }
            APPEND(newstr, len, cap, '"')
            for (j = 0; j < str->length; j++) {
              /* Make sure we escape all escape sequences */
              switch (str->ptr[j]) {
                case 0:
                  APPEND(newstr, len, cap, '\\');
                  APPEND(newstr, len, cap, '0');
                  APPEND(newstr, len, cap, '0');
                  APPEND(newstr, len, cap, '0');
                  break;

                case '"':
                  APPEND(newstr, len, cap, '\\');
                  APPEND(newstr, len, cap, '"');
                  break;

                case '\n':
                  APPEND(newstr, len, cap, '\\');
                  APPEND(newstr, len, cap, '\n');
                  break;

                case '\\':
                  APPEND(newstr, len, cap, '\\');
                  APPEND(newstr, len, cap, '\\');
                  break;

                default:
                  APPEND(newstr, len, cap, str->ptr[j]);
                  break;
              }
            }
            APPEND(newstr, len, cap, '"');
            break;

          default:
            panic("%%s expects a string, not a %d", lv_gettype(arg));
        }
        break;
      }

      default:
        panic("bad string.format() mode: %c", fmt[i]);
    }
  }

  APPEND(newstr, len, cap, 0);

  lstate_return1(lv_string(lstr_add(newstr, len - 1, TRUE)));
}

static u32 lua_string_rep(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  size_t n = (size_t) lstate_getnumber(1);
  /* Avoid malloc if we can */
  if (n == 0 || str->length == 0) {
    lstate_return1(str_empty);
  }
  size_t len = n * str->length;

  char *newstr = xmalloc(len + 1);
  char *ptr = newstr;
  while (n-- > 0) {
    memcpy(ptr, str->ptr, str->length);
    ptr += str->length;
  }
  newstr[len] = 0;

  lstate_return1(lv_string(lstr_add(newstr, len, TRUE)));
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
  char *newstr = xmalloc(len + 1);
  memcpy(newstr, str->ptr + start, len);
  newstr[len] = 0;

  lstate_return1(lv_string(lstr_add(newstr, len, TRUE)));
}

static u32 lua_string_len(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  lstate_return1(lv_number((double) str->length));
}

static u32 lua_string_lower(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  if (str->length == 0) { lstate_return1(str_empty); }
  size_t i;
  char *newstr = xmalloc(str->length + 1);
  for (i = 0; i < str->length; i++) {
    newstr[i] = (char) tolower(str->ptr[i]);
  }
  newstr[i] = 0;
  lstate_return1(lv_string(lstr_add(newstr, i, TRUE)));
}

static u32 lua_string_upper(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  if (str->length == 0) { lstate_return1(str_empty); }
  size_t i;
  char *newstr = xmalloc(str->length + 1);
  for (i = 0; i < str->length; i++) {
    newstr[i] = (char) toupper(str->ptr[i]);
  }
  newstr[i] = 0;
  lstate_return1(lv_string(lstr_add(newstr, i, TRUE)));
}

static u32 lua_string_reverse(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  if (str->length == 0) { lstate_return1(str_empty); }
  size_t i;
  char *newstr = xmalloc(str->length + 1);
  for (i = 0; i < str->length; i++) {
    newstr[i] = str->ptr[str->length - i - 1];
  }
  newstr[i] = 0;
  lstate_return1(lv_string(lstr_add(newstr, i, TRUE)));
}

static u32 lua_string_byte(LSTATE) {
  lstring_t *str = lstate_getstring(0);
  ssize_t i, j, len = (ssize_t) str->length;
  i = argc < 2 || argv[1] == LUAV_NIL ? 1 : (ssize_t) lstate_getnumber(1);
  j = argc < 3 || argv[2] == LUAV_NIL ? i : (ssize_t) lstate_getnumber(2);

  FIX_INDICES(i, j, len);

  u32 k;
  if (j < i) { return 0; }
  for (k = 0; k < retc && k <= j - i; k++) {
    /* All bytes are considered unsigned, so we need to cast from char to u8 */
    retv[k] = lv_number((u8) str->ptr[i + k]);
  }
  return k;
}

static u32 lua_string_char(LSTATE) {
  if (argc == 0) {
    lstate_return1(str_empty);
  }
  char *str = xmalloc(argc + 1);
  u32 i;
  for (i = 0; i < argc; i++) {
    str[i] = (char) lstate_getnumber(i);
  }
  str[i] = 0;
  lstate_return1(lv_string(lstr_add(str, argc, TRUE)));
}
