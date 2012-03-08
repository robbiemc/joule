#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "error.h"
#include "flags.h"
#include "gc.h"
#include "lhash.h"
#include "luav.h"
#include "meta.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

/* Macros for dealing with the stack (accessing, setting) */
#define STACKI(n) (stack + (n))
#define STACK(n) vm_stack->base[STACKI(n)]
#define CONST(n) ({ assert((n) < func->num_consts); func->consts[n]; })
#define REG(n)                                                                \
  ({                                                                          \
    assert(&STACK(n) < vm_stack->top);                                        \
    luav _tmp = STACK(n);                                                     \
    lv_isupvalue(_tmp) ? *lv_getupvalue(_tmp) : _tmp;                         \
  })
#define SETREG(n, v)                                       \
  ({                                                       \
    assert(&STACK(n) < vm_stack->top);                     \
    luav tmp = STACK(n);                                   \
    if (lv_isupvalue(tmp)) {                               \
      *lv_getupvalue(tmp) = v;                             \
    } else {                                               \
      STACK(n) = v;                                        \
    }                                                      \
  })
#define KREG(n) ((n) >= 256 ? CONST((n) - 256) : REG(n))
#define UPVALUE(closure, n)                                \
  ({                                                       \
    assert((n) < (closure)->function.lua->num_upvalues);   \
    (closure)->upvalues[n];                                \
  })

/* Metatable macros */
#define BINOP_ADD(a,b) ((a)+(b))
#define BINOP_SUB(a,b) ((a)-(b))
#define BINOP_MUL(a,b) ((a)*(b))
#define BINOP_DIV(a,b) ((a)/(b))
#define BINOP_MOD(a,b) ((a) - floor((a)/(b))*(b))
#define BINOP_POW(a,b) (pow((a), (b)))
#define BINOP_EQ(a,b)  ((a) == (b))
#define BINOP_LT(a,b)  ((a) <  (b))
#define BINOP_LE(a,b)  ((a) <= (b))
#define META_ARITH_BINARY(op, idx) {                                \
          a = A(code);                                              \
          luav bv = KREG(B(code));                                  \
          luav cv = KREG(C(code));                                  \
          if (lv_isnumber(bv) && lv_isnumber(cv)) {                 \
            SETREG(a, lv_number(op(lv_cvt(bv), lv_cvt(cv))));       \
            break;                                                  \
          }                                                         \
          if (meta_binary(bv, idx, bv, cv, STACKI(a), &frame) ||    \
              meta_binary(cv, idx, bv, cv, STACKI(a), &frame))      \
            break;                                                  \
          double bd = lv_castnumber(bv, 0);                         \
          double cd = lv_castnumber(cv, 1);                         \
          SETREG(a, lv_number(op(bd, cd)));                         \
        }
#define META_COMPARE(op, idx) {                                           \
          u32 lt; luav res;                                               \
          luav bv = KREG(B(code)); luav cv = KREG(C(code));               \
          if (lv_sametyp(bv, cv) &&                                       \
              (lv_isnumber(bv) || lv_isstring(bv))) {                     \
            lt = (u8) op(lv_compare(bv, cv), 0);                          \
          } else if (meta_eq(bv, cv, idx, &res)) {                        \
            lt = lv_getbool(res, 0);                                      \
          } else if (idx == META_LE && meta_eq(cv, bv, META_LT, &res)) {  \
            lt = (u8) !lv_getbool(res, 0);                                \
          } else {                                                        \
            lt = (u8) op(lv_compare(bv, cv), 0);                          \
          }                                                               \
          if (lt != A(code)) {                                            \
            instrs++;                                                     \
          }                                                               \
        }

lhash_t *userdata_meta;      //<! metatables for all existing userdata
lhash_t *lua_globals;        //<! default global environment
lhash_t *global_env = NULL;  //<! current global environment
lframe_t *vm_running = NULL; //<! currently running function's frame
lstack_t *vm_stack;          //<! current stack, changes on thread switches
static lstack_t init_stack;  //<! initial stack

static u32 op_close(u32 upc, luav *upv);
static int meta_unary(luav operand, luav method, u32 reti, lframe_t *frame);
static int meta_binary(luav operand, luav method, luav lv, luav rv,
                       u32 reti, lframe_t *frame);
static int meta_eq(luav operand1, luav operand2, luav method, luav *ret);
static luav meta_lhash_get(luav operand, luav key, lframe_t *frame);
static void meta_lhash_set(luav operand, luav key, luav val, lframe_t *frame);
static u32  meta_call(luav value, u32 argc, u32 argvi, u32 retc, u32 retvi);
static luav meta_concat(luav v1, luav v2);
static void vm_gc();

/**
 * @brief Initializes all global structures
 *
 * Requres that the strings have been initialized
 */
EARLY(100) static void vm_setup() {
  lua_globals = lhash_alloc();
  userdata_meta = lhash_alloc();
  lhash_set(lua_globals, LSTR("_VERSION"), LSTR("Joule 0.0"));

  vm_stack_init(&init_stack, VM_STACK_INIT);
  vm_stack = &init_stack;
  gc_add_hook(vm_gc);
}

DESTROY static void vm_destroy() {
  vm_stack_destroy(&init_stack);
}

static void vm_gc() {
  /* Traverse all our globals */
  gc_traverse_stack(&init_stack);
  gc_traverse_pointer(lua_globals, LTABLE);
  gc_traverse_pointer(userdata_meta, LTABLE);
  gc_traverse_pointer(global_env, LTABLE);

  /* Keep the call stack around */
  lframe_t *frame;
  for (frame = vm_running; frame != NULL; frame = frame->caller) {
    gc_traverse_pointer(frame->closure, LFUNCTION);
  }
}

/**
 * @brief Creates a closure for the specified function
 */
lclosure_t* cfunc_alloc(cfunction_t *f, char *name, int upvalues) {
  cfunc_t *cf  = gc_alloc(sizeof(cfunc_t), LCFUNC);
  cf->f        = f;
  cf->upvalues = 0;
  cf->name     = name;

  lclosure_t *closure = gc_alloc(CLOSURE_SIZE(0), LFUNCTION);
  closure->type = LUAF_C;
  closure->function.c = cf;
  closure->env = lua_globals;
  return closure;
}

/**
 * @brief Register a C-function in a hash table with the given name
 *
 * @param table the table to add the function to
 * @param name the name for the function, and key in the table
 * @param f the c-function which is to be added
 */
void cfunc_register(lhash_t *table, char *name, cfunction_t *f) {
  lclosure_t *closure = cfunc_alloc(f, name, 0);
  lhash_set(table, lv_string(lstr_literal(name, FALSE)), lv_function(closure));
}

/**
 * @brief Initialize a lua stack
 *
 * @param stack the stack to initialize
 * @param size the initial size of the stack
 */
void vm_stack_init(lstack_t *stack, u32 size) {
  stack->size  = 0;
  stack->limit = size;
  stack->base  = xmalloc(sizeof(luav) * size);
  stack->top   = stack->base;
}

/**
 * @brief Destroys a lua stack
 *
 * @param stack pointer to the stack to be destroyed
 * @return 0 on success, negative error code on failure
 */
void vm_stack_destroy(lstack_t *stack) {
  free(stack->base);
  // zero things out just be be safe
  stack->size  = 0;
  stack->limit = 0;
  stack->base  = NULL;
  stack->top   = NULL;
}

/**
 * @brief Grow a stack by a given amount
 *
 * This should be called only if the current frame needs some more space, to
 * grab a new frame, call vm_stack_alloc()
 *
 * @param stack the stack to grow
 * @param amt the amount of stack slots to add
 */
void vm_stack_grow(lstack_t *stack, u32 amt) {
  stack->size += amt;
  if (stack->size >= stack->limit) {
    stack->limit *= 2;
    stack->base = xrealloc(stack->base, stack->limit * sizeof(luav));
  }
  stack->top = stack->base + stack->size;
  lv_nilify(&stack->base[stack->size - amt], amt);
}

/**
 * @brief Allocate a new stack frame from the given stack
 *
 * @param stack the stack to allocate from
 * @param amt the amount of stack to allocate
 * @return the base of the stack, give this to vm_stack_dealloc when the stack
 *         is done with being used
 */
u32 vm_stack_alloc(lstack_t *stack, u32 amt) {
  u32 base = stack->size;
  vm_stack_grow(stack, amt);
  return base;
}

/**
 * @brief Deallocate the given stack to return the top of the stack to the given
 *        base
 *
 * @param stack the stack to deallocate from
 * @param base the return value from a previous call to vm_stack_alloc()
 */
void vm_stack_dealloc(lstack_t *stack, u32 base) {
  stack->size = base;
  if (stack->size > VM_STACK_INIT && stack->size < stack->limit / 2) {
    stack->limit /= 2;
    stack->base = xrealloc(stack->base, stack->limit * sizeof(luav));
  }
  stack->top = stack->base + stack->size;
}

/**
 * @brief Run a lua function, entry point into the VM
 *
 * @param func the function to run, typically from a parsed lua file.
 */
void vm_run(lfunc_t *func) {
  /* set up the initial closure, then hit go */
  lclosure_t *closure = gc_alloc(sizeof(lclosure_t), LFUNCTION);
  closure->function.lua = func;
  closure->type = LUAF_LUA;
  closure->env  = lua_globals;
  global_env    = lua_globals;
  assert(func->num_upvalues == 0);

  vm_fun(closure, NULL, 0, 0, 0, 0);
}

/**
 * @brief Run a function, dispatching based off the type of the function
 *
 * @param closure the closure to run
 * @param parent the caller of this function, used to create stack traces
 * @param LSTATE the lua state being invoked
 */
u32 vm_fun(lclosure_t *closure, lframe_t *parent, LSTATE) {
  lframe_t frame;

  /* If we hit a TAILCALL, we don't want to continue to allocate stack, so
     get the stack before the 'top:' label. Also, no need to get stack if we're
     a C function */
  u32 stack = vm_stack->size;
  if (closure->type == LUAF_LUA) {
    stack = vm_stack_alloc(vm_stack, (u32) closure->function.lua->max_stack);
  }
  /* TAILCALL works by packing all of the arguments into the current stack
     frame, and then growing the stack for the next call frame. This means
     that the stack to deallocate to can change, so keep a copy of the absolute
     original bottom of the stack to deallocate to */
  u32 stack_orig = stack;
  /* Entry point of TAILCALL */
top:
  frame.caller  = parent;
  frame.closure = closure;
  vm_running    = &frame;

  /* handle c functions */
  if (closure->type != LUAF_LUA) {
    u32 ret = closure->function.c->f(argc, argvi, retc, retvi);
    vm_running = parent;
    if (stack != stack_orig) {
      vm_stack_dealloc(vm_stack, stack_orig);
    }
    return ret;
  }

  u32 i, a, b, c, bx, limit;
  u32 last_ret = 0;
  u32 upvalues = 0;
  lfunc_t *func = closure->function.lua;
  u32 *instrs = func->instrs;
  frame.pc = &instrs;
  luav temp;
  assert(closure->env != NULL);

  /* Copy all arguments onto our stack, located in the lowest registers.
     Might need to grow the stack if we're a VARARG function, and otherwise
     might need to clip our number of parameters. */
  if (func->is_vararg) {
    if (argc > func->max_stack) {
      vm_stack_grow(vm_stack, argc - func->max_stack);
    }
  } else if (argc > func->num_parameters) {
    argc = func->num_parameters;
  }
  memcpy(&STACK(0), &vm_stack->base[argvi], sizeof(luav) * argc);
  assert(&STACK(argc) <= vm_stack->top);
  lv_nilify(&STACK(argc), vm_stack->size - argc - stack);

  /* Aligned memory accesses are much better */
  assert((((size_t) instrs) & 3) == 0);

  /* Core VM loop, also really slow VM loop... */
  while (1) {
    assert(instrs < func->instrs + func->num_instrs);
    u32 code = *instrs++;

#ifndef NDEBUG
    if (flags.print) {
      size_t idx = ((size_t) instrs - (size_t) func->instrs) / sizeof(u32);
      printf("[%d] ", func->lines[idx]);
      opcode_dump(stdout, code);
      printf("\n");
    }
#endif

    /* Dispatch based on what we're supposed to be doing */
    switch (OP(code)) {
      /* R[A] = GLOBALS[CONST[BX]] */
      case OP_GETGLOBAL: {
        luav key = CONST(BX(code));
        assert(lv_isstring(key));
        luav val = meta_lhash_get(lv_table(closure->env), key, &frame);
        SETREG(A(code), val);
        break;
      }

      /* GLOBALS[CONST[BX]] = R[A] */
      case OP_SETGLOBAL: {
        luav key = CONST(BX(code));
        luav value = REG(A(code));
        meta_lhash_set(lv_table(closure->env), key, value, &frame);
        gc_check();
        break;
      }

      /* R[A] = R[B][R[C]] */
      case OP_GETTABLE: {
        luav table = REG(B(code));
        luav key = KREG(C(code));
        SETREG(A(code), meta_lhash_get(table, key, &frame));
        break;
      }

      /* R[A][R[B]] = R[C] */
      case OP_SETTABLE: {
        luav table = REG(A(code));
        luav key = KREG(B(code));
        luav value = KREG(C(code));
        meta_lhash_set(table, key, value, &frame);
        gc_check();
        break;
      }

      /* R[A] = UPVALUES[B], see OP_CLOSURE */
      case OP_GETUPVAL:
        temp = UPVALUE(closure, B(code));
        SETREG(A(code), *lv_getupvalue(temp));
        break;

      /* UPVALUES[B] = R[A], see OP_CLOSURE */
      case OP_SETUPVAL:
        temp = UPVALUE(closure, B(code));
        *lv_getupvalue(temp) = REG(A(code));
        break;

      /* R[A] = CONST[BX] */
      case OP_LOADK:
        SETREG(A(code), CONST(BX(code)));
        break;

      /* R[A..B] = nil */
      case OP_LOADNIL:
        for (i = A(code); i <= B(code); i++) {
          SETREG(i, LUAV_NIL);
        }
        break;

      /* R[A] = R[B] */
      case OP_MOVE:
        SETREG(A(code), REG(B(code)));
        break;

      /* Call a closure in a register, with a glob of parameters, receiving a
         glob of return values */
      case OP_CALL: {
        a = A(code); b = B(code); c = C(code);
        /* If we don't know how many arguments we're giving, then it's the last
           number of arguments received from some previous instruction */
        u32 num_args = b == 0 ? last_ret - a - 1 : b - 1;
        /* If we don't know how many return values we want, just say we want
           everything ever, our stack will be grown for us by whomever is
           returning value to us */
        u32 want_ret = c == 0 ? UINT_MAX : c - 1;
        u32 got;
        luav av = REG(a);
        lhash_t *meta = getmetatable(av);

        /* Dispatch metatable __call if we can, otherwise recurse on vm_fun */
        if (meta != NULL) {
          got = meta_call(av, num_args, STACKI(a + 1),
                              want_ret, STACKI(a));
        } else {
          lclosure_t *closure2 = lv_getfunction(REG(a), 0);
          got = vm_fun(closure2, &frame, num_args, STACKI(a + 1),
                                         want_ret, STACKI(a));
        }
        /* If we didn't get all the return values we wanted, then we need to
           make sure we set all extra values to nil */
        for (i = got; i < c - 1 && &STACK(a + i) < vm_stack->top; i++) {
          SETREG(a + i, LUAV_NIL);
        }
        /* Save how many things we just got, in case the next instruction
           doesn't know how many things it wants */
        last_ret = a + got;
        break;
      }

      /* Only exit point of the VM loop, returns a glob of parameters */
      case OP_RETURN:
        a = A(code);
        b = B(code);
        /* If we don't know what we're returning, then some previous instruction
           received a glob of parameters and kept track of what it got */
        limit = b == 0 ? last_ret - a : b - 1;
        /* the RETURN opcode implicitly performs a CLOSE operation */
        if (upvalues > 0) {
          upvalues -= op_close(vm_stack->size - stack, vm_stack->base + stack);
        }
        /* TODO: does this need to grow the stack? */
        for (i = 0; i < limit && i < retc; i++) {
          vm_stack->base[retvi + i] = REG(a + i);
        }
        /* reset the currently running frame */
        vm_running = parent;
        /* make sure we don't deallocate past the arguments returned */
        vm_stack_dealloc(vm_stack, MAX(retvi + i, stack_orig));
        return i;

      /* Slightly optimized version of a CALL, but doesn't need any extra stack.
         Implemented as a goto to the top of the VM loop, redoing all
         intiaizliation.

         Interesting part is preserving stacks. Arguments to the function need
         to be on a separate portion of the stack than the running function's
         stack frame, because it can call the VARARG instruction halfway in the
         function to grab the arguments. To do this, all arguments to the
         function are packed on the stack starting from stack_orig, and then
         the new stack is allocated from the top of that stack. */
      case OP_TAILCALL: {
        a = A(code);
        b = B(code);
        assert(C(code) == 0);
        /* Figure out how big our glob is and where it's located */
        argc = (b == 0 ? last_ret : a + b) - a - 1;
        argvi = STACKI(a + 1);
        luav av = REG(a);
        lhash_t *meta = getmetatable(av);
        /* As with CALL, dispatch the __call metamethod */
        if (meta != NULL) {
          /* TODO: bad error message? */
          closure = lv_getfunction(lhash_get(meta, META_CALL), 0);
          /* metamethod __call requires one extra argument, the table itself */
          vm_stack_grow(vm_stack, 1);
          memmove(&vm_stack->base[stack_orig + 1], &vm_stack->base[argvi],
                  argc * sizeof(luav));
          vm_stack->base[stack_orig] = av;
          argc++;
        } else {
          closure = lv_getfunction(av, 0);
          memmove(&vm_stack->base[stack_orig], &vm_stack->base[argvi],
                  argc * sizeof(luav));
        }
        argvi = stack_orig;
        /* Bring the stack down to the top of our args */
        vm_stack_dealloc(vm_stack, argvi + argc);

        /* reallocate stack for lua functions */
        if (closure->type == LUAF_LUA) {
          stack = vm_stack_alloc(vm_stack, closure->function.lua->max_stack);
        }
        goto top;
      }

      /* Create a lua function type, so it can be called. Possibly creates
         upvalues for the function, which are variables that are defined outside
         the scope of the function, but used inside the function.

         Upvalues are implemented as a special type of luav that is a pointer
         to another luav. These values are never exposed in lua, because they're
         normally only stored in the upvalues array of closures. The only
         difference is the fact that future references to the upvalue created
         in the function that created the closure must update the upvalue, or
         use the value that had been altered by a closure.

         Hence, the values on the stack that are converted to upvalues are
         replaced on the stack as an upvalue.*/
      case OP_CLOSURE: {
        bx = BX(code);
        assert(bx < func->num_funcs);
        lfunc_t *function = func->funcs[bx];
        lclosure_t *closure2 = gc_alloc(CLOSURE_SIZE(function->num_upvalues),
                                        LFUNCTION);
        SETREG(A(code), lv_function(closure2));
        closure2->type = LUAF_LUA;
        closure2->function.lua = function;
        closure2->env = closure->env;
        lv_nilify(closure2->upvalues, function->num_upvalues);

        for (i = 0; i < function->num_upvalues; i++) {
          u32 pseudo = *instrs++;
          luav upvalue;
          if (OP(pseudo) == OP_MOVE) {
            /* Can't use the REG macro because we don't want to duplicate
               upvalues. If the register on the stack is an upvalue, we want to
               use the same upvalue... */
            assert(&STACK(B(pseudo)) < vm_stack->top);
            temp = STACK(B(pseudo));
            if (lv_isupvalue(temp)) {
              upvalue = temp;
            } else {
              /* If the stack register is not an upvalue, we need to promote it
                 to an upvalue, thereby scribbling over our stack variable so
                 it's now considered an upvalue */
              luav *ptr = gc_alloc(sizeof(luav), LUPVALUE);
              *ptr = temp;
              upvalue = lv_upvalue(ptr);
              STACK(B(pseudo)) = upvalue;
            }
            upvalues++;
          } else {
            upvalue = UPVALUE(closure, B(pseudo));
          }

          /* The allocated closure needs a reference to the upvalue */
          closure2->upvalues[i] = upvalue;
        }
        gc_check();
        break;
      }

      case OP_CLOSE:
        a = A(code);
        upvalues -= op_close(vm_stack->size - stack - a, &STACK(a));
        break;

      case OP_JMP:
        instrs += SBX(code);
        break;

      case OP_EQ: {
        luav res;
        luav bv = KREG(B(code));
        luav cv = KREG(C(code));
        u32 eq = (u32) ((bv != LUAV_NAN) && (bv == cv));
        if (!eq && meta_eq(bv, cv, META_EQ, &res))
          eq = lv_getbool(res, 0);
        if (eq != A(code))
          instrs++;
        break;
      }

      case OP_LT: META_COMPARE(BINOP_LT, META_LT); break;
      case OP_LE: META_COMPARE(BINOP_LE, META_LE); break;

      case OP_TEST:
        temp = REG(A(code));
        if (lv_getbool(temp, 0) != C(code)) {
          instrs++;
        }
        break;

      case OP_TESTSET:
        temp = REG(B(code));
        if (lv_getbool(temp, 0) == C(code)) {
          SETREG(A(code), temp);
        } else {
          instrs++;
        }
        break;

      case OP_LOADBOOL:
        SETREG(A(code), lv_bool((u8) B(code)));
        if (C(code)) {
          instrs++;
        }
        break;

      case OP_ADD: META_ARITH_BINARY(BINOP_ADD, META_ADD); break;
      case OP_SUB: META_ARITH_BINARY(BINOP_SUB, META_SUB); break;
      case OP_MUL: META_ARITH_BINARY(BINOP_MUL, META_MUL); break;
      case OP_DIV: META_ARITH_BINARY(BINOP_DIV, META_DIV); break;
      case OP_MOD: META_ARITH_BINARY(BINOP_MOD, META_MOD); break;
      case OP_POW: META_ARITH_BINARY(BINOP_POW, META_POW); break;

      case OP_UNM: {
        a = A(code);
        luav bv = REG(B(code));
        if (lv_isnumber(bv)) {
          SETREG(a, lv_number(-lv_cvt(bv)));
          break;
        }
        if (meta_unary(bv, META_UNM, STACKI(a), &frame))
          break;
        SETREG(a, lv_number(-lv_castnumber(bv, 0)));
        break;
      }

      case OP_NOT: { // no metamethod for not
        u8 bv = lv_getbool(REG(B(code)), 0);
        SETREG(A(code), lv_bool(bv ^ 1));
        break;
      }

      case OP_LEN: {
        luav bv = REG(B(code));
        switch (lv_gettype(bv)) {
          case LSTRING: {
            size_t len = lv_caststring(bv, 0)->length;
            SETREG(A(code), lv_number((double) len));
            break;
          }
          case LTABLE: {
            u64 len = lv_gettable(bv, 0)->length;
            SETREG(A(code), lv_number((double) len));
            break;
          }
          default:
            panic("Invalid type for len\n");
        }
        break;
      }

      case OP_NEWTABLE: {
        // TODO - We can't currently create a table of a certain size, so we
        //        ignore the size hints. Eventually we should use them.
        SETREG(A(code), lv_table(lhash_alloc()));
        gc_check();
        break;
      }

      case OP_FORPREP:
        a = A(code);
        SETREG(a, lv_number(lv_castnumber(REG(a), 0) -
                                  lv_castnumber(REG(a + 2), 0)));
        instrs += SBX(code);
        break;

      case OP_FORLOOP: {
        a = A(code);
        double d1 = lv_castnumber(REG(a), 0);
        double d2 = lv_castnumber(REG(a + 1), 0);
        double step = lv_castnumber(REG(a + 2), 0);
        SETREG(a, lv_number(d1 + step));
        d1 += step;
        if ((step > 0 && d1 <= d2) || (step < 0 && d1 >= d2)) {
          SETREG(a + 3, lv_number(d1));
          instrs += SBX(code);
        }
        break;
      }

      case OP_CONCAT: {
        b = B(code);
        c = C(code);
        luav value = REG(b);

        for (i = b + 1; i <= c; i++) {
          value = meta_concat(value, REG(i));
        }

        SETREG(A(code), value);
        gc_check();
        break;
      }

      case OP_SETLIST: {
        b = B(code);
        a = A(code);
        c = C(code);
        if (c == 0) {
          c = *instrs++;
        }
        if (b == 0) { b = last_ret - a - 1; }
        lhash_t *hash = lv_gettable(REG(a), 0);
        for (i = 1; i <= b; i++) {
          lhash_set(hash, lv_number((c - 1) * LFIELDS_PER_FLUSH + i),
                          REG(a + i));
        }
        break;
      }

      case OP_VARARG: {
        a = A(code);
        b = B(code);
        u32 limit = b > 0 ? b - 1 : argc - func->num_parameters;
        if (stack + limit + a > vm_stack->size) {
          vm_stack_grow(vm_stack, stack + limit + a - vm_stack->size);
        }
        for (i = 0; i < limit && i < argc; i++) {
          SETREG(a + i, vm_stack->base[argvi + i + func->num_parameters]);
        }
        for (; i < limit; i++) {
          SETREG(a + i, LUAV_NIL);
        }
        last_ret = a + i;
        break;
      }

      case OP_SELF: {
        luav bv = REG(B(code));
        SETREG(A(code) + 1, bv);
        luav val = meta_lhash_get(bv, KREG(C(code)), &frame);
        SETREG(A(code), val);
        break;
      }

      case OP_TFORLOOP:
        a = A(code); c = C(code);
        lclosure_t *closure2 = lv_getfunction(REG(a), 0);
        u32 got = vm_fun(closure2, &frame, 2, STACKI(a + 1),
                                   c, STACKI(a + 3));
        vm_running = &frame;
        temp = REG(a + 3);
        if (got == 0 || temp == LUAV_NIL) {
          instrs++;
        } else {
          SETREG(a + 2, temp);
        }
        // fill in the nils
        if (c != 0) {
          for (i = got; i < c; i++) {
            SETREG(a + 3 + i, LUAV_NIL);
          }
        }
        break;

      default:
        fprintf(stderr, "Unimplemented opcode: ");
        opcode_dump(stderr, code);
        abort();
    }
  }
}

static u32 op_close(u32 upc, luav *upv) {
  u32 i, r = 0;
  for (i = 0; i < upc; i++) {
    if (lv_isupvalue(upv[i])) {
      upv[i] = *lv_getupvalue(upv[i]);
      r++;
    }
  }
  return r;
}

static int meta_unary(luav operand, luav name, u32 reti, lframe_t *frame) {
  lhash_t *meta = getmetatable(operand);
  if (meta != NULL) {
    luav method = lhash_get(meta, name);
    if (method != LUAV_NIL) {
      u32 idx = vm_stack_alloc(vm_stack, 1);
      vm_stack->base[idx] = operand;
      u32 got = vm_fun(lv_getfunction(method, 0), frame, 1, idx, 1, reti);
      vm_stack_dealloc(vm_stack, idx);
      if (got == 0)
        vm_stack->base[reti] = LUAV_NIL;
      return TRUE;
    }
  }
  return FALSE;
}

static int meta_binary(luav operand, luav name, luav lv, luav rv,
                       u32 reti, lframe_t *frame) {
  lhash_t *meta = getmetatable(operand);
  if (meta != NULL) {
    luav method = lhash_get(meta, name);
    if (method != LUAV_NIL) {
      u32 idx = vm_stack_alloc(vm_stack, 2);
      vm_stack->base[idx] = lv;
      vm_stack->base[idx + 1] = rv;
      u32 got = vm_fun(lv_getfunction(method, 0), frame, 2, idx, 1, reti);
      vm_stack_dealloc(vm_stack, idx);
      if (got == 0)
        vm_stack->base[reti] = LUAV_NIL;
      return TRUE;
    }
  }
  return FALSE;
}

static int meta_eq(luav operand1, luav operand2, luav name, luav *ret) {
  lhash_t *meta1 = getmetatable(operand1);
  lhash_t *meta2 = getmetatable(operand2);

  if (meta1 != NULL && meta2 != NULL) {
    luav meth1 = lhash_get(meta1, name);
    luav meth2 = lhash_get(meta2, name);

    if (meth1 != LUAV_NIL && meth1 == meth2) {
      u32 idx = vm_stack_alloc(vm_stack, 3);
      vm_stack->base[idx] = operand1;
      vm_stack->base[idx + 1] = operand2;
      u32 got = vm_fun(lv_getfunction(meth1, 0), vm_running,
                       2, idx, 1, idx + 2);
      if (got == 0) {
        *ret = LUAV_NIL;
      } else {
        *ret = vm_stack->base[idx + 2];
      }
      vm_stack_dealloc(vm_stack, idx);
      return TRUE;
    }
  }
  return FALSE;
}

static luav meta_lhash_get(luav operand, luav key, lframe_t *frame) {
  int istable = lv_istable(operand);

  if (istable) {
    lhash_t *table = lv_getptr(operand);
    if (table->metatable == NULL) {
      return lhash_get(table, key);
    }
    luav val = lhash_get(table, key);
    if (val != LUAV_NIL) return val;
  }

  lhash_t *meta = getmetatable(operand);
  if (meta == NULL) goto notfound;

  luav method = lhash_get(meta, META_INDEX);
  if (method == LUAV_NIL) goto notfound;
  if (!lv_isfunction(method))
    return meta_lhash_get(method, key, frame);

  u32 idx = vm_stack_alloc(vm_stack, 3);
  vm_stack->base[idx] = operand;
  vm_stack->base[idx + 1] = key;
  u32 got = vm_fun(lv_getfunction(method, 0), frame, 2, idx, 1, idx + 2);
  luav val = vm_stack->base[idx + 2];
  vm_stack_dealloc(vm_stack, idx);
  if (got == 0) return LUAV_NIL;
  return val;

notfound:
  if (istable) return LUAV_NIL;
  err_rawstr("metatable.__index not found", TRUE);
}

static void meta_lhash_set(luav operand, luav key, luav val, lframe_t *frame) {
  lhash_t *meta = getmetatable(operand);
  if (meta == NULL) goto normal;

  if (lv_istable(operand) && lhash_get(TBL(operand), key) != LUAV_NIL)
    goto normal;

  luav method = lhash_get(meta, META_NEWINDEX);
  if (method == LUAV_NIL) goto normal;
  if (!lv_isfunction(method))
    return meta_lhash_set(method, key, val, frame);

  u32 idx = vm_stack_alloc(vm_stack, 3);
  vm_stack->base[idx] = operand;
  vm_stack->base[idx + 1] = key;
  vm_stack->base[idx + 2] = val;
  vm_fun(lv_getptr(method), frame, 3, idx, 0, 0);
  vm_stack_dealloc(vm_stack, idx);
  return;

normal:
  lhash_set(TBL(operand), key, val);
}

static u32 meta_call(luav value, u32 argc, u32 argvi, u32 retc, u32 retvi) {
  lhash_t *meta = getmetatable(value);
  luav method = meta == NULL ? LUAV_NIL : lhash_get(meta, META_CALL);
  if (method == LUAV_NIL) {
    err_rawstr("metatable.__call not found", TRUE);
  }
  u32 idx = vm_stack_alloc(vm_stack, argc + 1);
  /* TODO: bad error message? */
  lclosure_t *func = lv_getfunction(method, 0);
  memcpy(&vm_stack->base[idx + 1], &vm_stack->base[argvi], argc * sizeof(luav));
  vm_stack->base[idx] = value;
  u32 ret = vm_fun(func, vm_running, argc + 1, idx, retc, retvi);
  vm_stack_dealloc(vm_stack, idx);
  return ret;
}

static luav meta_concat(luav v1, luav v2) {
  lhash_t *meta = getmetatable(v1);
  luav method = meta == NULL ? LUAV_NIL : lhash_get(meta, META_CONCAT);
  if (meta == NULL || method == LUAV_NIL) {
    meta = getmetatable(v2);
    method = meta == NULL ? LUAV_NIL : lhash_get(meta, META_CONCAT);
  }

  if (meta != NULL && method != LUAV_NIL) {
    /* TODO: better error message? */
    lclosure_t *func = lv_getfunction(method, 0);
    u32 idx = vm_stack_alloc(vm_stack, 2);
    vm_stack->base[idx] = v1;
    vm_stack->base[idx + 1] = v2;
    u32 got = vm_fun(func, vm_running, 2, idx, 1, idx);
    luav ret = LUAV_NIL;
    if (got > 0) {
      ret = vm_stack->base[idx];
    }
    vm_stack_dealloc(vm_stack, idx);
    return ret;
  }

  return lv_concat(v1, v2);
}
