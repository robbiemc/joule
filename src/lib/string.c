#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lhash.h"
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

#define GETNUM(v) lv_getnumber(lv_tonumber(v, 10))

static lhash_t lua_string;
static luav lua_string_format(u32 argc, luav *argv);
static luav lua_string_rep(luav string, luav n);

static LUAF_VARARG(lua_string_format);
static LUAF_2ARG(lua_string_rep);

INIT static void lua_string_init() {
  lhash_init(&lua_string);
  lhash_set(&lua_string, LSTR("format"), lv_function(&lua_string_format_f));
  lhash_set(&lua_string, LSTR("rep"),    lv_function(&lua_string_rep_f));

  lhash_set(&lua_globals, LSTR("string"), lv_table(&lua_string));
}

DESTROY static void lua_string_destroy() {
  lhash_free(&lua_string);
}

static luav lua_string_format(u32 argc, luav *argv) {
  assert(argc > 0);
  lstring_t *lfmt = lstr_get(lv_getstring(argv[0]));
  size_t len = 0, cap = LUAV_INIT_STRING;
  char *newstr = xmalloc(cap);
  char *fmt = lfmt->ptr;
  u32 i, j, argi = 1;
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
    for (j = 0; j < 3 && isdigit(fmt[i]); j++) i++; /* skip the width */
    assert(!isdigit(fmt[i]));
    if (fmt[i] == '.') i++;                         /* skip period format */
    for (j = 0; j < 3 && isdigit(fmt[i]); j++) i++; /* skip the precision */
    assert(!isdigit(fmt[i]));

    strncpy(buf, pct_start, i - start + 1);
    buf[i - start + 1] = 0;

    assert(argi < argc);
    switch (fmt[i]) {
      case 'c':
        SNPRINTF(newstr, len, cap, buf, (char) GETNUM(argv[argi++]));
        break;

      case 'i':
      case 'd':
        SNPRINTF(newstr, len, cap, buf, (int) GETNUM(argv[argi++]));
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
        SNPRINTF(newstr, len, cap, buf, (size_t) GETNUM(argv[argi++]));
        break;
      }

      case 'e':
      case 'E':
      case 'f':
      case 'g':
      case 'G':
        SNPRINTF(newstr, len, cap, buf, GETNUM(argv[argi++]));
        break;

      case 'q':
      case 's': {
        luav arg = argv[argi++];
        lstring_t *str;
        switch (lv_gettype(arg)) {
          /* Numbers are coerced to strings, but nothing else is... */
          case LNUMBER:
            buf[i - start - 1] = 'f';
            SNPRINTF(newstr, len, cap, buf, lv_getnumber(arg));
            break;

          case LSTRING:
            str = lstr_get(lv_getstring(arg));
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

  return lv_string(lstr_add(newstr, len - 1, TRUE));
}

static luav lua_string_rep(luav string, luav _n) {
  lstring_t *str = lstr_get(lv_getstring(string));
  size_t n = (size_t) lv_getnumber(_n);
  size_t len = n * str->length;

  char *newstr = xmalloc(len + 1);
  char *ptr = newstr;
  while (n-- > 0) {
    memcpy(ptr, str->ptr, str->length);
    ptr += str->length;
  }
  newstr[len] = 0;

  return lv_string(lstr_add(newstr, len, TRUE));
}
