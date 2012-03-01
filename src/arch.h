#ifndef _ARCH_H
#define _ARCH_H

#if !defined(__x86_64) && !defined(__i386)
#error Current architecture not supported
#endif

#if defined(__x86_64)
# define CALLEE_REGS 6
#elif defined(__i386)
# define CALLEE_REGS 4
#endif

void arch_coroutine_swap(void **stacksave, void *newstack);

#endif /* _ARCH_H */
