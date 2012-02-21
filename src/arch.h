#ifndef _ARCH_H
#define _ARCH_H

#ifndef __x86_64
#error Nothing works on archs other than x86-64 yet.
#endif

#define CALLEE_REGS 6

#define get_sp() ({ void* rsp; asm ("mov %%rsp, %0" : "=g" (rsp)); rsp; })

void save_callee_regs(void *data);
void assume_callee_regs(void *data);
void coroutine_swap_asm(void **stacksave, void *newstack);

#endif /* _ARCH_H */
