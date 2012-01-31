#ifndef _ERROR_H
#define _ERROR_H

#include "config.h"

struct lclosure;

extern char *lua_program;

void err_explain(int err, struct lclosure *closure) NORETURN;
void err_missing(u32 n, u32 expected_type) NORETURN;
void err_badtype(u32 n, u32 expected_type, u32 got_type) NORETURN;

#endif /* _ERROR_H */
