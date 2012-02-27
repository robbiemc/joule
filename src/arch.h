#ifndef _ARCH_H
#define _ARCH_H

#ifndef __x86_64
#error Nothing works on archs other than x86-64 yet.
#endif

#define get_sp() ({ void* rsp; asm ("mov %%rsp, %0" : "=g" (rsp)); rsp; })

void coroutine_swap_asm(void **stacksave, void *newstack);

#endif /* _ARCH_H */
