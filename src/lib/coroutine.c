#include <limits.h>
#include <string.h>
#include <sys/mman.h>

#include "debug.h"
#include "lhash.h"
#include "lib/coroutine.h"
#include "panic.h"
#include "vm.h"

#ifndef __x86_64
#error Coroutines do not work an archs other than x86-64 yet.
#endif

#define CO_RUNNING    0
#define CO_SUSPENDED  1
#define CO_NORMAL     2
#define CO_DEAD       3
#define CO_NEVER_RUN  4

#define CO_STACK_SIZE (16 * 1024)

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

static lhash_t    lua_coroutine;
static lthread_t  main_thread;
static lstack_t  *main_stack;
static lthread_t *cur_thread;

static luav str_running;
static luav str_suspended;
static luav str_normal;
static luav str_dead;

extern void coroutine_swap_asm(void **stacksave, void *newstack);
static u32 lua_co_create(LSTATE);
static u32 lua_co_running(LSTATE);
static u32 lua_co_resume(LSTATE);
static u32 lua_co_status(LSTATE);
static u32 lua_co_wrap(LSTATE);
static u32 co_wrap_trampoline(LSTATE);
static u32 co_wrap_helper(lthread_t *co, LSTATE);
static u32 lua_co_yield(LSTATE);
static LUAF(lua_co_create);
static LUAF(lua_co_resume);
static LUAF(lua_co_running);
static LUAF(lua_co_status);
static LUAF(lua_co_wrap);
static LUAF(lua_co_yield);
static cfunc_t co_wrapper_cf = {.f = co_wrap_trampoline, .name = "do not see"};

INIT static void lua_coroutine_init() {
  str_running   = LSTR("running");
  str_suspended = LSTR("suspended");
  str_normal    = LSTR("normal");
  str_dead      = LSTR("dead");
  cur_thread = &main_thread;
  main_thread.env = &lua_globals;
  main_stack = vm_stack;

  lhash_init(&lua_coroutine);
  REGISTER(&lua_coroutine, "create",  &lua_co_create_f);
  REGISTER(&lua_coroutine, "resume",  &lua_co_resume_f);
  REGISTER(&lua_coroutine, "running", &lua_co_running_f);
  REGISTER(&lua_coroutine, "status",  &lua_co_status_f);
  REGISTER(&lua_coroutine, "wrap",    &lua_co_wrap_f);
  REGISTER(&lua_coroutine, "yield",   &lua_co_yield_f);

  lhash_set(&lua_globals, LSTR("coroutine"), lv_table(&lua_coroutine));
}

DESTROY static void lua_coroutine_destroy() {
  lhash_free(&lua_coroutine);
}

void coroutine_changeenv(lthread_t *to) {
  lthread_t *old = cur_thread;
  old->env = global_env;
  xassert(to != NULL);
  xassert(to != old);
  xassert(to->status != CO_RUNNING);
  xassert(to->status != CO_DEAD);
  cur_thread = to;
  to->status = CO_RUNNING;
  global_env = to->env;
  if (to == &main_thread) {
    vm_stack = main_stack;
  } else {
    vm_stack = &to->vm_stack;
  }
}

static void coroutine_swap(lthread_t *to) {
  lthread_t *old = cur_thread;
  coroutine_changeenv(to);
  coroutine_swap_asm(&old->curstack, to->curstack);
}

/**
 * @brief Entry point of all coroutines, delegates upward to vm_fun, and then
 *        essentially calls coroutine.yield() upon return
 */
static void coroutine_wrapper() {
  xassert(cur_thread->status == CO_RUNNING);
  u32 retc = vm_fun(cur_thread->closure, vm_running,
                    cur_thread->argc, cur_thread->argvi,
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
  lthread_t *thread = xmalloc(sizeof(lthread_t));
  thread->status = CO_NEVER_RUN;
  thread->stack  = mmap(NULL, CO_STACK_SIZE, PROT_WRITE | PROT_READ,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
  xassert(thread->stack != MAP_FAILED);

  thread->caller  = NULL;
  thread->closure = function;
  thread->env     = cur_thread->env;
  vm_stack_init(&thread->vm_stack, 20);

  u64 *stack = (u64*) ((u64) thread->stack + CO_STACK_SIZE);
  /* Bogus return address, and then actual address to return to */
  *(stack - 1) = 0xdeadd00ddeadd00dULL;
  *(stack - 2) = (u64) coroutine_wrapper;
  thread->curstack = stack - 8; /* 6 callee regs, and two return addresses */

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
  if (cur_thread == &main_thread) {
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
  lclosure_t *closure = xmalloc(CLOSURE_SIZE(1));
  closure->type = LUAF_C;
  closure->function.c = &co_wrapper_cf;
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

  if (thread->status == CO_DEAD) {
    xassert(munmap(thread->stack, CO_STACK_SIZE) == 0);
  }

  /* Gather all the return values from yield() */
  for (i = 0; i < retc && i < thread->retc; i++) {
    lstate_return(thread->vm_stack.base[thread->retvi + i], i);
  }

  return i;
}

static u32 lua_co_yield(LSTATE) {
  /* Tell coroutine.resume() where to find its return values, and where to put
     further arguments to this thread */
  cur_thread->retc = argc;
  cur_thread->retvi = argvi;
  cur_thread->argc = retc;
  cur_thread->argvi = retvi;

  if (cur_thread->status != CO_DEAD) {
    cur_thread->status = CO_SUSPENDED;
  }
  coroutine_swap(cur_thread->caller);
  return cur_thread->argc;
}
