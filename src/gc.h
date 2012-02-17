#ifndef _GC_H_
#define _GC_H_

#include "vm.h"

void *gc_alloc(size_t size);
void *gc_calloc(size_t nelt, size_t eltsize);
void *gc_realloc(void *addr, size_t newsize);
void garbage_collect();

#endif /* _GC_H_ */
