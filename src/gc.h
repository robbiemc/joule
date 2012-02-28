#ifndef _GC_H_
#define _GC_H_

#include "luav.h"
#include "vm.h"

typedef void(gchook_t)(void);

/* Set up a hook to be run during gc, meant to be used to update static/global
   variables in a file */
void gc_add_hook(gchook_t *hook);

/* Allocation */
void* gc_alloc(size_t size, int type);
void* gc_realloc(void *addr, size_t newsize);

void garbage_collect(void);

/* Traversal functions */
void gc_traverse(luav value);
void gc_traverse_pointer(void *ptr, int type);
void gc_traverse_stack(lstack_t *stack);

#endif /* _GC_H_ */
