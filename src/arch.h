#ifndef _ARCH_H
#define _ARCH_H

#ifndef __x86_64
#error Nothing works on archs other than x86-64 yet.
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

void arch_coroutine_swap(void **stacksave, void *newstack);

#endif /* _ARCH_H */
