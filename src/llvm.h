#ifndef _LLVM_H_
#define _LLVM_H_

struct lfunc;
struct lclosure;

typedef struct jfunc {
  void *binary;
  void *value;
} jfunc_t;

#include "lstate.h"

#define JSTACKI 0
#define JARGC   1
#define JARGVI  2
#define JRETC   3
#define JRETVI  4
#define JARGS   5

// This the value to set an instruction's run count to if
// it can't be compiled yet
#define INVAL_RUN_COUNT 250
// Code won't be compiled unless it's been run this many times
#define COMPILE_COUNT 3

void llvm_init();
void llvm_destroy();

i32  llvm_compile(struct lfunc *func, u32 start, u32 end, luav *stack, jfunc_t *jfun);
i32  llvm_run(jfunc_t *func, struct lclosure *closure, u32 *args);
void llvm_free(jfunc_t *jfun);

#endif /* _LLVM_H_ */
