#ifndef _LIB_COROUTINE_H
#define _LIB_COROUTINE_H

#include "lhash.h"
#include "vm.h"

typedef struct lthread {
  int status;           //<! Current status of the thread
  void *stack;          //<! Absolute bottom of the C-stack

  /* Passing/returning arguments, all indices are relative to this thread's
     stack located below */
  u32 argc;
  u32 argvi;
  u32 retc;
  u32 retvi;

  struct lthread *caller; //<! Thread who resumed us
  lclosure_t *closure;    //<! Closure we are running
  lframe_t *frame;        //<! Current frame
  void *curstack;         //<! Last saved c-stack
  lhash_t *env;           //<! Lua environment
  lstack_t vm_stack;      //<! Lua stack
} lthread_t;

lthread_t* coroutine_current(void);
void       coroutine_changeenv(lthread_t *to);
void       coroutine_free(lthread_t *thread);

#endif /* _LIB_COROUTINE_H */
