#ifndef _LLVM_H_
#define _LLVM_H_

struct lfunc;
struct lclosure;

typedef void jfunc_t;

void llvm_init();
void llvm_destroy();

jfunc_t *llvm_compile(struct lfunc *func, u32 start, u32 end);
u32 llvm_run(jfunc_t *func, struct lclosure *closure, u32 stacki);

#endif /* _LLVM_H_ */
