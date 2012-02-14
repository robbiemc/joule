#ifndef _GC_H_
#define _GC_H_

#include "vm.h"

void *gc_alloc(size_t size);
void *gc_calloc(size_t nelt, size_t eltsize);
void *gc_realloc(void *addr, size_t newsize);
void gc_free(void *addr);
void garbage_collect(luav *start, lstack_t *init_stack, lframe_t *running);

#endif /* _GC_H_ */
