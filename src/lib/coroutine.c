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
static luav lua_co_create(luav function);
static luav lua_co_running(void);
static u32  lua_co_resume(u32 argc, luav *argv, u32 retc, luav *retv);
static luav lua_co_status(luav co);
static luav lua_co_wrap(luav function);
static u32  co_wrap_helper(luav co, u32 argc, luav *argv, u32 retc, luav *retv);
static u32  lua_co_yield(u32 argc, luav *argv, u32 retc, luav *retv);
static LUAF_1ARG(lua_co_create);
static LUAF_VARRET(lua_co_resume);
static LUAF_0ARG(lua_co_running);
static LUAF_1ARG(lua_co_status);
static LUAF_1ARG(lua_co_wrap);
static LUAF_VARRET(lua_co_yield);

INIT static void lua_coroutine_init() {
  str_running   = LSTR("running");
  str_suspended = LSTR("suspended");
  str_normal    = LSTR("normal");
  str_dead      = LSTR("dead");
  cur_thread = &main_thread;
  lhash_init(&lua_coroutine);
  lhash_set(&lua_coroutine, LSTR("create"),  lv_function(&lua_co_create_f));
  lhash_set(&lua_coroutine, LSTR("resume"),  lv_function(&lua_co_resume_f));
  lhash_set(&lua_coroutine, LSTR("running"), lv_function(&lua_co_running_f));
  lhash_set(&lua_coroutine, LSTR("status"),  lv_function(&lua_co_status_f));
  lhash_set(&lua_coroutine, LSTR("wrap"),    lv_function(&lua_co_wrap_f));
  lhash_set(&lua_coroutine, LSTR("yield"),   lv_function(&lua_co_yield_f));

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

static luav lua_co_create(luav function) {
  lthread_t *thread = xmalloc(sizeof(lthread_t));
  thread->status = CO_NEVER_RUN;
  thread->stack  = mmap(NULL, CO_STACK_SIZE, PROT_WRITE | PROT_READ,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
  assert(thread->stack != MAP_FAILED);

  thread->caller  = NULL;
  thread->closure = lv_getfunction(function);
  assert(thread->closure->type == LUAF_LUA);

  u64 *stack = (u64*) ((u64) thread->stack + CO_STACK_SIZE);
  /* Bogus return address, and then actual address to return to */
  *(stack - 1) = 0xdeadd00ddeadd00dULL;
  *(stack - 2) = (u64) coroutine_wrapper;
  thread->curstack = stack - 8; /* 6 callee regs, and two return addresses */

  return lv_thread(thread);
}

static u32 lua_co_resume(u32 argc, luav *argv, u32 retc, luav *retv) {
  assert(argc > 0);
  lthread_t *thread = lv_getthread(argv[0]);
  if (thread->status == CO_DEAD) {
    switch (retc) {
      default:
      case 2:
        retv[1] = LSTR("cannot resume dead coroutine");
      case 1:
        retv[0] = LUAV_FALSE;
      case 0:
        break;
    }
    return retc > 2 ? 2 : retc;
  }
  if (retc > 0) {
    u32 amt = co_wrap_helper(argv[0], argc - 1, argv + 1, retc - 1, retv + 1);
    retv[0] = LUAV_TRUE;
    return amt + 1;
  }
  return co_wrap_helper(argv[0], argc - 1, argv + 1, retc, retv);
}

static luav lua_co_running() {
  if (cur_thread == &main_thread) {
    return LUAV_NIL;
  }
  return lv_thread(cur_thread);
}

static luav lua_co_status(luav co) {
  lthread_t *thread = lv_getthread(co);
  switch (thread->status) {
    case CO_RUNNING:      return str_running;
    case CO_NEVER_RUN:
    case CO_SUSPENDED:    return str_suspended;
    case CO_NORMAL:       return str_normal;
    case CO_DEAD:         return str_dead;
  }

  panic("Invalid thread status: %d", thread->status);
}

static luav lua_co_wrap(luav function) {
  luav routine = lua_co_create(function);
  lclosure_t *closure = xmalloc(CLOSURE_SIZE(1));
  closure->type = LUAF_C_BOUND;
  closure->function.boundarg = co_wrap_helper;
  closure->upvalues[0] = routine;
  return lv_function(closure);
}

static u32 co_wrap_helper(luav co, u32 argc, luav *argv, u32 retc, luav *retv) {
  lthread_t *thread = lv_getthread(co);

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

static u32 lua_co_yield(u32 argc, luav *argv, u32 retc, luav *retv) {
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
