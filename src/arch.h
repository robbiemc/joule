#ifndef _ARCH_H
#define _ARCH_H

#ifndef __x86_64
#error Nothing works on archs other than x86-64 yet.
#endif

#define get_sp() ({ void* rsp; asm ("mov %%rsp, %0" : "=g" (rsp)); rsp; })

void arch_coroutine_swap(void **stacksave, void *newstack);

#endif /* _ARCH_H */
