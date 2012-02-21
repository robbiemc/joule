#ifndef _GC_H_
#define _GC_H_

#include "vm.h"

void *gc_alloc(size_t size, int type);
void *gc_calloc(size_t nelt, size_t eltsize, int type);
void *gc_realloc(void *addr, size_t newsize);
void *gc_relocate(void *addr);
void  gc_set_bottom(void);
void garbage_collect();

#endif /* _GC_H_ */
