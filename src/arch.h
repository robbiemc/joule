#ifndef _ARCH_H
#define _ARCH_H

#ifndef __x86_64
#error Nothing works on archs other than x86-64 yet.
#endif

#define CALLER_REGS 6

/* One extra space for the return address, and one for the stack pointer */
typedef size_t caller_regs_t[CALLER_REGS + 2];

#define get_sp() ({ void* rsp; asm ("mov %%rsp, %0" : "=g" (rsp)); rsp; })

void arch_coroutine_swap(void **stacksave, void *newstack);
int  arch_save_callee(caller_regs_t *regs);
void arch_assume_callee(caller_regs_t *regs);

#endif /* _ARCH_H */
