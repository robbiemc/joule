#ifndef _ERROR_H
#define _ERROR_H

#include <setjmp.h>

#include "config.h"

#define ERRBUF_SIZE 200

struct lframe;

extern char *lua_program;
extern char err_desc[ERRBUF_SIZE];
extern jmp_buf *err_catcher;

char *err_typestr(u32 typ);
void err_explain(int err, struct lframe *frame) NORETURN;
void err_missing(u32 n, u32 expected_type) NORETURN;
void err_badtype(u32 n, u32 expected_type, u32 got_type) NORETURN;
void err_str(u32 n, char *explain) NORETURN;
void err_rawstr(char *explain) NORETURN;

#endif /* _ERROR_H */
