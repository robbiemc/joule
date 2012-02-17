#ifndef _LIB_COROUTINE_H
#define _LIB_COROUTINE_H

#include "lhash.h"
#include "vm.h"

typedef struct lthread {
  int status;
  void *stack;

  u32 argc;
  u32 argvi;
  u32 retc;
  u32 retvi;

  struct lthread *caller;
  lclosure_t *closure;
  void *curstack;
  lhash_t *env;
  lstack_t vm_stack;
} lthread_t;

struct lthread* coroutine_current(void);
void            coroutine_changeenv(struct lthread *to);

#endif /* _LIB_COROUTINE_H */
