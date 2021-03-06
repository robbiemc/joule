#ifndef _ERROR_H
#define _ERROR_H

#include <setjmp.h>

#include "config.h"
#include "luav.h"

#define ERRBUF_SIZE 200

#define ONERR(try, catch, errvar) {             \
    jmp_buf onerr;                              \
    jmp_buf *prev = err_catcher;                \
    static struct lthread *env;                 \
    err_catcher = &onerr;                       \
    env = coroutine_current();                  \
    if (_setjmp(onerr) == 0) {                  \
      { try }                                   \
      errvar = 0;                               \
    } else {                                    \
      if (env != coroutine_current()) {         \
        coroutine_changeenv(env);               \
      }                                         \
      { catch }                                 \
      errvar = 1;                               \
    }                                           \
    err_catcher = prev;                         \
  }

struct lframe;

extern char *lua_program;
extern luav err_value;
extern jmp_buf *err_catcher;

char *err_typestr(u32 typ);
void err_explain(int err, struct lframe *frame) NORETURN;
void err_missing(u32 n, u32 expected_type) NORETURN;
void err_badtype(u32 n, u32 expected_type, u32 got_type) NORETURN;
void err_str(u32 n, char *explain) NORETURN;
void err_rawstr(char *explain, int withpos) NORETURN;
void err_luav(struct lframe *frame, luav v) NORETURN;

#endif /* _ERROR_H */
