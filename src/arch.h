#ifndef _ARCH_H
#define _ARCH_H

#if !defined(__x86_64) && !defined(__i386)
#error Don't currently support this architecture
#endif

#ifdef __APPLE__
# include <mach-o/getsect.h>
# define exec_etext() ((void*) get_etext())
# define exec_edata() ((void*) get_edata())
# define exec_end()   ((void*) get_end())
#else
extern char etext, edata, end;
# define exec_etext() ((void*) &etext)
# define exec_edata() ((void*) &edata)
# define exec_end()   ((void*) &end)
#endif

#if defined(__x86_64)
# define CALLEE_REGS 6
#elif defined(__i386)
# define CALLEE_REGS 4
#endif

void arch_coroutine_swap(void **stacksave, void *newstack);

#endif /* _ARCH_H */
