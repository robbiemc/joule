#include <limits.h>
#include <string.h>
#include <sys/mman.h>

#include "arch.h"
#include "debug.h"
#include "gc.h"
#include "lib/coroutine.h"
#include "panic.h"

#define CO_RUNNING    0
#define CO_SUSPENDED  1
#define CO_NORMAL     2
#define CO_DEAD       3
#define CO_NEVER_RUN  4

/* Currently, stacks are fairly large to allow for compiling. LLVM compilation
   has been seen to need at least 64k to avoid stack overflow */
#define CO_STACK_SIZE (128 * 1024)

static lhash_t   *lua_coroutine;
static lthread_t *main_thread;
static lstack_t  *main_stack;
static lthread_t *cur_thread;

static luav str_running;
static luav str_suspended;
static luav str_normal;
static luav str_dead;

static u32 lua_co_create(LSTATE);
static u32 lua_co_running(LSTATE);
static u32 lua_co_resume(LSTATE);
static u32 lua_co_status(LSTATE);
static u32 lua_co_wrap(LSTATE);
static u32 co_wrap_trampoline(LSTATE);
static u32 co_wrap_helper(lthread_t *co, LSTATE);
static u32 lua_co_yield(LSTATE);
static cfunc_t *co_wrapper_cf;
static void coroutine_gc();

INIT static void lua_coroutine_init() {
  str_running   = LSTR("running");
  str_suspended = LSTR("suspended");
  str_normal    = LSTR("normal");
  str_dead      = LSTR("dead");
  main_thread   = gc_alloc(sizeof(lthread_t), LTHREAD);
  cur_thread    = main_thread;
  main_stack    = vm_stack;

  main_thread->env           = lua_globals;
  main_thread->caller        = NULL;
  main_thread->closure       = NULL;
  main_thread->frame         = NULL;
  main_thread->vm_stack.size = 0;
  main_thread->stack         = NULL;

  lua_coroutine = lhash_alloc();
  cfunc_register(lua_coroutine, "create",  lua_co_create);
  cfunc_register(lua_coroutine, "resume",  lua_co_resume);
  cfunc_register(lua_coroutine, "running", lua_co_running);
  cfunc_register(lua_coroutine, "status",  lua_co_status);
  cfunc_register(lua_coroutine, "wrap",    lua_co_wrap);
  cfunc_register(lua_coroutine, "yield",   lua_co_yield);

  lhash_set(lua_globals, LSTR("coroutine"), lv_table(lua_coroutine));
  gc_add_hook(coroutine_gc);

  co_wrapper_cf = gc_alloc(sizeof(cfunc_t), LCFUNC);
  co_wrapper_cf->f = co_wrap_trampoline;
  co_wrapper_cf->upvalues = 1;
  co_wrapper_cf->name = "do not see";
}

static void coroutine_gc() {
  gc_traverse_pointer(cur_thread, LTHREAD);
  gc_traverse_pointer(main_thread, LTHREAD);
  gc_traverse_pointer(co_wrapper_cf, LCFUNC);
}

void coroutine_changeenv(lthread_t *to) {
  lthread_t *old = cur_thread;
  old->env = global_env;
  old->frame = vm_running;
  xassert(to != NULL);
  xassert(to != old);
  xassert(to->status != CO_RUNNING);
  xassert(to->status != CO_DEAD);
  cur_thread = to;
  to->status = CO_RUNNING;
  vm_running = to->frame;
  global_env = to->env;
  if (to == main_thread) {
    vm_stack = main_stack;
  } else {
    vm_stack = &to->vm_stack;
  }
}

static void coroutine_swap(lthread_t *to) {
  lthread_t *old = cur_thread;
  coroutine_changeenv(to);
  arch_coroutine_swap(&old->curstack, to->curstack);
}

/**
 * @brief Entry point of all coroutines, delegates upward to vm_fun, and then
 *        essentially calls coroutine.yield() upon return
 */
static void coroutine_wrapper() {
  xassert(cur_thread->status == CO_RUNNING);
  u32 retc = vm_fun(cur_thread->closure, cur_thread->argc, cur_thread->argvi,
                    UINT_MAX, 0);
  cur_thread->status = CO_DEAD;
  lua_co_yield(retc, 0, 0, 0);
  /* We should never be resumed to here */
  panic("coroutine went too far");
}

static u32 lua_co_create(LSTATE) {
  lclosure_t *function = lstate_getfunction(0);
  if (function->type != LUAF_LUA) {
    err_str(0, "Lua function expected");
  }
  lthread_t *thread = gc_alloc(sizeof(lthread_t), LTHREAD);
  thread->status = CO_NEVER_RUN;
  thread->stack  = mmap(NULL, CO_STACK_SIZE, PROT_WRITE | PROT_READ,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
  xassert(thread->stack != MAP_FAILED);

  thread->caller  = NULL;
  thread->closure = function;
  thread->frame   = NULL;
  thread->env     = cur_thread->env;
  thread->argvi   = 0;
  thread->argc    = 0;
  vm_stack_init(&thread->vm_stack, 20);

  size_t *stack = (size_t*) ((size_t) thread->stack + CO_STACK_SIZE);
  /* Bogus return address, and then actual address to return to */
  size_t spaces = CALLEE_REGS + 2;
  memset(stack - spaces, 0, sizeof(size_t) * spaces);
  *(stack - 1) = 0;
  *(stack - 2) = (size_t) coroutine_wrapper;
  thread->curstack = stack - spaces;

  lstate_return1(lv_thread(thread));
}

static u32 lua_co_resume(LSTATE) {
  lthread_t *thread = lstate_getthread(0);
  int iserr;
  u32 ret;

  ONERR({
    if (retc > 0) {
      ret = co_wrap_helper(thread, argc - 1, argvi + 1, retc - 1, retvi + 1);
    } else {
      ret = co_wrap_helper(thread, argc - 1, argvi + 1, retc, retvi);
    }
  }, {
    if (thread != cur_thread) {
      thread->caller = NULL;
      thread->status = CO_DEAD;
      cur_thread->status = CO_RUNNING;
    }
  }, iserr);

  if (iserr) {
    lstate_return(LUAV_FALSE, 0);
    lstate_return(err_value, 1);
    return 2;
  } else {
    lstate_return(LUAV_TRUE, 0);
  }
  return ret + 1;
}

lthread_t* coroutine_current() {
  return cur_thread;
}

static u32 lua_co_running(LSTATE) {
  if (cur_thread == main_thread) {
    lstate_return1(LUAV_NIL);
  }
  lstate_return1(lv_thread(cur_thread));
}

static u32 lua_co_status(LSTATE) {
  lthread_t *thread = lstate_getthread(0);
  switch (thread->status) {
    case CO_RUNNING:      lstate_return1(str_running);
    case CO_NEVER_RUN:
    case CO_SUSPENDED:    lstate_return1(str_suspended);
    case CO_NORMAL:       lstate_return1(str_normal);
    case CO_DEAD:         lstate_return1(str_dead);
  }

  panic("Invalid thread status: %d", thread->status);
}

static u32 co_wrap_trampoline(LSTATE) {
  lthread_t *thread = lv_getthread(vm_running->closure->upvalues[0], 0);
  return co_wrap_helper(thread, argc, argvi, retc, retvi);
}

static u32 lua_co_wrap(LSTATE) {
  luav routine;
  u32 idx = vm_stack_alloc(vm_stack, 1);
  lua_co_create(argc, argvi, 1, idx);
  routine = vm_stack->base[idx];
  vm_stack_dealloc(vm_stack, idx);
  lclosure_t *closure = gc_alloc(CLOSURE_SIZE(1), LFUNCTION);
  closure->type = LUAF_C;
  closure->function.c = co_wrapper_cf;
  /* vm_running is coroutine.wrap(), which has no environment */
  xassert(vm_running->caller != NULL);
  closure->env = vm_running->caller->closure->env;
  xassert(closure->env != NULL);
  closure->upvalues[0] = routine;
  lstate_return1(lv_function(closure));
}

static u32 co_wrap_helper(lthread_t *thread, LSTATE) {
  u32 i;

  if (thread->status == CO_DEAD) {
    err_rawstr("cannot resume dead coroutine", FALSE);
  } else if (thread == cur_thread) {
    err_rawstr("cannot resume running coroutine", FALSE);
  } else if (thread->status == CO_NEVER_RUN) {
    /* If this thread has no run before, it's argc/argv will be passed to the
       initial function. A call to coroutine.yield() will fill in the retc/retv
       automatically, setting thread->retc to how many return values were
       actally placed into the array */
    u32 idx = vm_stack_alloc(&thread->vm_stack, argc);
    for (i = 0; i < argc; i++) {
      thread->vm_stack.base[idx + i] = lstate_getval(i);
    }
    thread->argc = argc;
    thread->argvi = idx;
  } else {
    /* If the thread has already run, then we are resuming a previous
       coroutine.yield(). We need to fill in the retc/retv of the call to
       yield(), whose variables have been placed into the thread's argc/argv
       fields. */
    for (i = 0; i < argc && i < thread->argc; i++) {
      thread->vm_stack.base[thread->argvi + i] = lstate_getval(i);
    }
    thread->argc = i;
  }

  xassert(thread->status != CO_RUNNING);
  xassert(thread->caller == NULL);
  thread->caller = cur_thread;
  cur_thread->status = CO_NORMAL;
  coroutine_swap(thread);
  thread->caller = NULL;

  /* Gather all the return values from yield() */
  for (i = 0; i < retc && i < thread->retc; i++) {
    lstate_return(thread->vm_stack.base[thread->retvi + i], i);
  }

  if (thread->status == CO_DEAD) {
    coroutine_free(thread);
  }

  return i;
}

static u32 lua_co_yield(LSTATE) {
  /* Tell coroutine.resume() where to find its return values, and where to put
     further arguments to this thread */
  cur_thread->retc  = argc;
  cur_thread->retvi = argvi;
  cur_thread->argc  = retc;
  cur_thread->argvi = retvi;

  if (cur_thread->status != CO_DEAD) {
    cur_thread->status = CO_SUSPENDED;
  }
  coroutine_swap(cur_thread->caller);
  return cur_thread->argc;
}

/**
 * @brief Free resources associated with a coroutine.
 *
 * Doesn't deallocate the coroutine itself, but just the internals of the
 * thread.
 *
 * @param thread the thread to deallocate resources from
 */
void coroutine_free(lthread_t *thread) {
  if (thread->stack != NULL) {
    vm_stack_destroy(&thread->vm_stack);
    xassert(munmap(thread->stack, CO_STACK_SIZE) == 0);
    thread->stack = NULL;
  }
}
