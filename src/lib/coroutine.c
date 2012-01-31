#include <assert.h>
#include <string.h>
#include <sys/mman.h>

#include "debug.h"
#include "lhash.h"
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
#define CO_RETC       20

typedef struct lthread {
  int status;
  void *stack;

  u32 argc;
  luav *argv;
  u32 retc;
  luav *retv;

  struct lthread *caller;
  lclosure_t *closure;
  void *curstack;
} lthread_t;

static lhash_t    lua_coroutine;
static lthread_t  main_thread;
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

static void coroutine_swap(lthread_t *to) {
  lthread_t *old = cur_thread;
  assert(to != NULL);
  assert(to->status != CO_RUNNING);
  assert(to->status != CO_DEAD);
  cur_thread = to;
  to->status = CO_RUNNING;
  coroutine_swap_asm(&old->curstack, to->curstack);
}

/**
 * @brief Entry point of all coroutines, delegates upward to vm_fun, and then
 *        essentially calls coroutine.yield() upon return
 */
static void coroutine_wrapper() {
  luav retv[CO_RETC];
  assert(cur_thread->status == CO_RUNNING);
  u32 retc = vm_fun(cur_thread->closure,
                    cur_thread->argc, cur_thread->argv,
                    CO_RETC, retv);
  cur_thread->status = CO_DEAD;
  lua_co_yield(retc, retv, 0, NULL);
  /* We should never be resumed to here */
  panic("coroutine went too far");
}

static u32 lua_co_create(LSTATE) {
  lclosure_t *function = lstate_getfunction(0);
  assert(function->type == LUAF_LUA);
  lthread_t *thread = xmalloc(sizeof(lthread_t));
  thread->status = CO_NEVER_RUN;
  thread->stack  = mmap(NULL, CO_STACK_SIZE, PROT_WRITE | PROT_READ,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
  assert(thread->stack != MAP_FAILED);

  thread->caller  = NULL;
  thread->closure = function;

  u64 *stack = (u64*) ((u64) thread->stack + CO_STACK_SIZE);
  /* Bogus return address, and then actual address to return to */
  *(stack - 1) = 0xdeadd00ddeadd00dULL;
  *(stack - 2) = (u64) coroutine_wrapper;
  thread->curstack = stack - 8; /* 6 callee regs, and two return addresses */

  lstate_return1(lv_thread(thread));
}

static u32 lua_co_resume(LSTATE) {
  lthread_t *thread = lstate_getthread(0);
  if (thread->status == CO_DEAD) {
    lstate_return(LUAV_FALSE, 0);
    lstate_return(LSTR("cannot resume dead coroutine"), 1);
    return 2;
  }
  if (retc > 0) {
    u32 amt = co_wrap_helper(thread, argc - 1, argv + 1, retc - 1, retv + 1);
    retv[0] = LUAV_TRUE;
    return amt + 1;
  }
  return co_wrap_helper(thread, argc - 1, argv + 1, retc, retv);
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
  lthread_t *thread = lv_getthread(vm_running->upvalues[0], 0);
  return co_wrap_helper(thread, argc, argv, retc, retv);
}

static u32 lua_co_wrap(LSTATE) {
  luav routine;
  assert(lua_co_create(argc, argv, 1, &routine) == 1);
  lclosure_t *closure = xmalloc(CLOSURE_SIZE(1));
  closure->type = LUAF_C;
  closure->function.c = &co_wrapper_cf;
  closure->upvalues[0] = routine;
  lstate_return1(lv_function(closure));
}

static u32 co_wrap_helper(lthread_t *thread, LSTATE) {
  thread->retc = retc;
  thread->retv = retv;

  if (thread->status == CO_NEVER_RUN) {
    /* If this thread has no run before, it's argc/argv will be passed to the
       initial function. A call to coroutine.yield() will fill in the retc/retv
       automatically, setting thread->retc to how many return values were
       actally placed into the array */
    thread->argc = argc;
    thread->argv = argv;
  } else {
    /* If the thread has already run, then we are resuming a previous
       coroutine.yield(). We need to fill in the retc/retv of the call to
       yield(), whose variables have been placed into the thread's argc/argv
       fields. */
    u32 i;
    for (i = 0; i < argc && i < thread->argc; i++) {
      thread->argv[i] = argv[i];
    }
    thread->argc = i;
  }

  assert(thread->caller == NULL);
  thread->caller = cur_thread;
  cur_thread->status = CO_NORMAL;
  coroutine_swap(thread);
  thread->caller = NULL;

  return thread->retc;
}

static u32 lua_co_yield(LSTATE) {
  u32 i;
  /* Fill in return values from where coroutine.resume() was called a long
     time ago */
  for (i = 0; i < argc && i < cur_thread->retc; i++) {
    cur_thread->retv[i] = argv[i];
  }
  cur_thread->retc = i;
  /* Filled in during coroutine.resume() */
  cur_thread->argc = retc;
  cur_thread->argv = retv;

  if (cur_thread->status != CO_DEAD) {
    cur_thread->status = CO_SUSPENDED;
  }
  coroutine_swap(cur_thread->caller);
  return cur_thread->argc;
}
