#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>

#include "luav.h"
#include "vm.h"

void dbg_dump_function(FILE *out, lfunc_t *func);
void dbg_dump_luav(FILE *out, luav value);

#endif /* _DEBUG_H_ */
