#ifndef _LLVM_H_
#define _LLVM_H_

struct lfunc;
struct lclosure;

typedef void jfunc_t;

#include "lstate.h"

#define JSTACKI 0
#define JARGC   1
#define JARGVI  2
#define JRETC   3
#define JRETVI  4
#define JARGS   5

void llvm_init();
void llvm_destroy();

jfunc_t* llvm_compile(struct lfunc *func, u32 start, u32 end, luav *stack);
i32      llvm_run(jfunc_t *func, struct lclosure *closure, u32 *args);

#endif /* _LLVM_H_ */
