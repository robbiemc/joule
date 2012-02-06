#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "error.h"
#include "flags.h"
#include "lhash.h"
#include "luav.h"
#include "meta.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

#define STACKI(n) (stack + (n))
#define STACK(n) vm_stack->base[STACKI(n)]
#define CONST(n) ({ assert((n) < func->num_consts); func->consts[n]; })
#define REG(n)                                                                \
  ({                                                                          \
    assert(&STACK(n) < vm_stack->top);                                         \
    luav _tmp = STACK(n);                                                     \
    need_close && lv_isupvalue(_tmp) ? lv_getupvalue(_tmp)->value : _tmp;     \
  })
#define SETREG(n, v)                            \
  ({                                            \
    assert(&STACK(n) < vm_stack->top);           \
    if (need_close && lv_isupvalue(STACK(n))) { \
      lv_getupvalue(STACK(n))->value = v;       \
    } else {                                    \
      STACK(n) = v;                             \
    }                                           \
  })

/* TODO: extract out 256 */
#define KREG(n) ((n) >= 256 ? CONST((n) - 256) : REG(n))
#define UPVALUE(closure, n)                                \
  ({                                                       \
    assert((n) < (closure)->function.lua->num_upvalues);   \
    (closure)->upvalues[n];                                \
  })
#define DECODEFP8(v) (((u32)8 | ((v)&7)) << (((u32)(v)>>3) - 1))

/* Metatable macros */
#define TBL(x) lv_gettable(x,0)
#define getmetatable(v) (lv_istable(v)    ? TBL(v)->metatable :                \
                         lv_isuserdata(v) ? TBL(lhash_get(&userdata_meta, v)) :\
                         NULL)
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
#define META_COMPARE(op, idx) {                               \
          u32 lt; luav res;                                   \
          luav bv = KREG(B(code)); luav cv = KREG(C(code));   \
          if (meta_eq(bv, cv, idx, &res, &frame)) {           \
            lt = lv_getbool(res, 0);                          \
          } else {                                            \
            lt = op(lv_compare(bv, cv), 0);                   \
          }                                                   \
          if (lt != A(code)) {                                \
            instrs++;                                         \
          }                                                   \
        }

lhash_t userdata_meta;
lhash_t lua_globals;
lhash_t *global_env = NULL;
lframe_t *vm_running = NULL;
lstack_t *vm_stack;
static lstack_t init_stack;

static void op_close(u32 upc, luav *upv);
static int meta_unary(luav operand, u32 op, u32 reti, lframe_t *frame);
static int meta_binary(luav operand, u32 op, luav lv, luav rv,
                       u32 reti, lframe_t *frame);
static int meta_eq(luav operand1, luav operand2, u32 op,
                   luav *ret, lframe_t *frame);
static luav meta_lhash_get(luav operand, luav key, lframe_t *frame);
static void meta_lhash_set(luav operand, luav key, luav val, lframe_t *frame);

INIT static void vm_setup() {
  lhash_init(&userdata_meta);
  lhash_init(&lua_globals);
  lhash_set(&lua_globals, LSTR("_VERSION"), LSTR("Joule 0.0"));
  lhash_set(&lua_globals, LSTR("_G"), lv_table(&lua_globals));

  vm_stack_init(&init_stack, VM_STACK_INIT);
  vm_stack = &init_stack;
}

DESTROY static void vm_destroy() {
  lhash_free(&lua_globals);
}

void vm_stack_init(lstack_t *stack, u32 size) {
  stack->size = 0;
  stack->limit = size;
  stack->base = xmalloc(sizeof(luav) * size);
  stack->top = stack->base;
}

void vm_stack_grow(lstack_t *stack, u32 amt) {
  stack->size += amt;
  if (stack->size >= stack->limit) {
    stack->limit *= 2;
    stack->base = xrealloc(stack->base, stack->limit * sizeof(luav));
  }
  stack->top = stack->base + stack->size;
}

u32 vm_stack_alloc(lstack_t *stack, u32 amt) {
  u32 base = stack->size;
  vm_stack_grow(stack, amt);
  return base;
}

void vm_stack_dealloc(lstack_t *stack, u32 base) {
  stack->size = base;
  if (stack->size > VM_STACK_INIT && stack->size < stack->limit / 2) {
    stack->limit /= 2;
    stack->base = xrealloc(stack->base, stack->limit * sizeof(luav));
  }
  stack->top = stack->base + stack->size;
}

void vm_run(lfunc_t *func) {
  lclosure_t closure;
  closure.function.lua = func;
  closure.type = LUAF_LUA;
  closure.env = &lua_globals;
  global_env = &lua_globals;
  assert(func->num_upvalues == 0);
  vm_fun(&closure, NULL, 0, 0, 0, 0);
}

u32 vm_fun(lclosure_t *closure, lframe_t *parent, LSTATE) {
  lframe_t frame;

  u32 stack = vm_stack->size;
  if (closure->type == LUAF_LUA) {
    stack = vm_stack_alloc(vm_stack, (u32) closure->function.lua->max_stack);
  }
top:
  frame.caller  = parent;
  frame.closure = closure;
  vm_running    = &frame;

  // handle c functions
  if (closure->type != LUAF_LUA) {
    u32 ret = closure->function.c->f(argc, argvi, retc, retvi);
    vm_running = parent;
    vm_stack_dealloc(vm_stack, stack);
    return ret;
  }

  // it's a lua function
  u32 i, a, b, c, bx, limit;
  u32 last_ret = 0;
  u32 need_close = 0;
  lfunc_t *func = closure->function.lua;
  u32 *instrs = func->instrs;
  frame.pc = &instrs;
  u32 *end = instrs + func->num_instrs;
  luav temp;
  assert(closure->env != NULL);

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
  assert((((u64) instrs) & 3) == 0);
  while (instrs < end) {
    u32 code = *instrs++;

    if (flags.print) {
      u64 idx = ((u64)instrs - (u64)func->instrs) / sizeof(u32);
      printf("[%d] ", func->lines[idx]);
      opcode_dump(stdout, code);
      printf("\n");
    }

    switch (OP(code)) {
      case OP_GETGLOBAL: {
        luav key = CONST(BX(code));
        assert(lv_isstring(key));
        luav val = meta_lhash_get(lv_table(closure->env), key, &frame);
        SETREG(A(code), val);
        break;
      }

      case OP_SETGLOBAL: {
        luav key = CONST(BX(code));
        luav value = REG(A(code));
        meta_lhash_set(lv_table(closure->env), key, value, &frame);
        break;
      }

      case OP_GETTABLE: {
        luav table = REG(B(code));
        luav key = KREG(C(code));
        SETREG(A(code), meta_lhash_get(table, key, &frame));
        break;
      }

      case OP_SETTABLE: {
        luav table = REG(A(code));
        luav key = KREG(B(code));
        luav value = KREG(C(code));
        meta_lhash_set(table, key, value, &frame);
        break;
      }

      case OP_GETUPVAL:
        temp = UPVALUE(closure, B(code));
        SETREG(A(code), lv_getupvalue(temp)->value);
        break;

      case OP_SETUPVAL:
        temp = UPVALUE(closure, B(code));
        lv_getupvalue(temp)->value = REG(A(code));
        break;

      case OP_LOADK:
        SETREG(A(code), CONST(BX(code)));
        break;

      case OP_LOADNIL:
        for (i = A(code); i <= B(code); i++) {
          SETREG(i, LUAV_NIL);
        }
        break;

      case OP_MOVE:
        SETREG(A(code), REG(B(code)));
        break;

      case OP_CALL: {
        a = A(code); b = B(code); c = C(code);
        lclosure_t *closure2 = lv_getfunction(REG(a), 0);
        u32 num_args = b == 0 ? last_ret - a - 1 : b - 1;
        u32 want_ret = c == 0 ? UINT_MAX : c - 1;

        u32 got = vm_fun(closure2, &frame, num_args, STACKI(a + 1),
                                           want_ret, STACKI(a));
        // fill in the nils
        for (i = got; i < c - 1 && &STACK(a + i) < vm_stack->top; i++) {
          SETREG(a + i, LUAV_NIL);
        }
        last_ret = a + got;
        break;
      }

      case OP_RETURN:
        a = A(code);
        b = B(code);
        if (b == 0) {
          limit = last_ret - a;
        } else {
          limit = b - 1;
        }
        for (i = 0; i < limit && i < retc; i++) {
          vm_stack->base[retvi + i] = REG(a + i);
        }
        if (need_close) {
          op_close(vm_stack->size - stack, vm_stack->base + stack);
        }
        vm_running = parent;
        vm_stack_dealloc(vm_stack, max(retvi + i, stack));
        return i;

      case OP_TAILCALL:
        a = A(code);
        b = B(code);
        closure = lv_getfunction(REG(a), 0);
        if (closure->type == LUAF_LUA) {
          int diff = closure->function.lua->max_stack - func->max_stack;
          if (diff > 0) {
            vm_stack_grow(vm_stack, (u32) diff);
          }
        }
        assert(C(code) == 0);
        argc = (b == 0 ? last_ret : a + b) - a - 1;
        argvi = STACKI(a + 1);
        goto top;

      case OP_CLOSURE: {
        bx = BX(code);
        assert(bx < func->num_funcs);
        lfunc_t *function = &func->funcs[bx];
        lclosure_t *closure2 = xmalloc(CLOSURE_SIZE(function->num_upvalues));
        closure2->type = LUAF_LUA;
        closure2->function.lua = function;
        closure2->env = closure->env;
        need_close |= function->num_upvalues > 0;

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
              upvalue_t *ptr = xmalloc(sizeof(upvalue_t));
              ptr->refcnt = 1; /* one for us, one for them added later */
              ptr->value = temp;
              upvalue = lv_upvalue(ptr);
              STACK(B(pseudo)) = upvalue;
            }
          } else {
            upvalue = UPVALUE(closure, B(pseudo));
          }

          /* The allocated closure needs a reference to the upvalue */
          lv_getupvalue(upvalue)->refcnt++;
          closure2->upvalues[i] = upvalue;
        }

        SETREG(A(code), lv_function(closure2));
        break;
      }

      case OP_CLOSE:
        need_close = 0;
        op_close(vm_stack->size - stack - A(code), &STACK(A(code)));
        break;

      case OP_JMP:
        instrs += SBX(code);
        break;

      case OP_EQ: {
        luav res;
        luav bv = KREG(B(code));
        luav cv = KREG(C(code));
        u32 eq = (bv != LUAV_NAN) && (bv == cv);
        if (!eq && meta_eq(bv, cv, META_EQ, &res, &frame))
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
            size_t len = lv_gettable(bv, 0)->length;
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
        lhash_t *ht = xmalloc(sizeof(lhash_t));
        lhash_init(ht);
        SETREG(A(code), lv_table(ht));
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
        size_t len = 0;
        b = B(code);
        c = C(code);

        for (i = b; i <= c; i++) {
          len += lv_caststring(REG(i), i - b)->length;
        }

        char *str = xmalloc(len + 1);
        char *ptr = str;
        for (i = b; i <= c; i++) {
          lstring_t *lstr = lv_caststring(REG(i), i - b);
          memcpy(ptr, lstr->ptr, lstr->length);
          ptr += lstr->length;
        }
        *ptr = 0;
        SETREG(A(code), lv_string(lstr_add(str, len, TRUE)));
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
          SETREG(a + i, vm_stack->base[argvi + i]);
        }
        for (; i < limit; i++) {
          SETREG(a + i, LUAV_NIL);
        }
        last_ret = a + i;
        break;
      }

      case OP_SELF:
        SETREG(A(code) + 1, REG(B(code)));
        temp = KREG(C(code));
        temp = lhash_get(lv_gettable(REG(B(code)), 0), temp);
        SETREG(A(code), temp);
        break;

      case OP_TFORLOOP:
        a = A(code); c = C(code);
        lclosure_t *closure2 = lv_getfunction(REG(a), 0);
        u32 got = vm_fun(closure2, &frame, 2, STACKI(a + 1),
                                   (u32) REG(c), STACKI(a + 3));
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

  panic("ran out of opcodes!");
}

static void op_close(u32 upc, luav *upv) {
  u32 i;
  for (i = 0; i < upc; i++) {
    if (lv_isupvalue(upv[i])) {
      upvalue_t *upvalue = lv_getupvalue(upv[i]);
      if (--upvalue->refcnt == 0) {
        upv[i] = upvalue->value;
        free(upvalue);
      }
    }
  }
}

static int meta_unary(luav operand, u32 op, u32 reti, lframe_t *frame) {
  lhash_t *meta = getmetatable(operand);
  if (meta != NULL) {
    luav method = meta->metamethods[op];
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

static int meta_binary(luav operand, u32 op, luav lv, luav rv,
                       u32 reti, lframe_t *frame) {
  lhash_t *meta = getmetatable(operand);
  if (meta != NULL) {
    luav method = meta->metamethods[op];
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

static int meta_eq(luav operand1, luav operand2, u32 op,
                   luav *ret, lframe_t *frame) {
  lhash_t *meta1 = getmetatable(operand1);
  lhash_t *meta2 = getmetatable(operand2);
  if (meta1 != NULL && meta2 != NULL) {
    luav meth1 = meta1->metamethods[op];
    luav meth2 = meta2->metamethods[op];
    if (meth1 != LUAV_NIL && meth1 == meth2) {
      u32 idx = vm_stack_alloc(vm_stack, 3);
      vm_stack->base[idx] = operand1;
      vm_stack->base[idx + 1] = operand2;
      u32 got = vm_fun(lv_getfunction(meth1, 0), frame, 2, idx, 1, idx + 2);
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
    lhash_t *table = lv_gettable(operand, 0);
    luav val = lhash_get(table, key);
    if (val != LUAV_NIL) return val;
  }

  lhash_t *meta = getmetatable(operand);
  if (meta == NULL) goto notfound;

  luav method = meta->metamethods[META_INDEX];
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

  luav method = meta->metamethods[META_NEWINDEX];
  if (method == LUAV_NIL) goto normal;
  if (!lv_isfunction(method))
    return meta_lhash_set(method, key, val, frame);

  u32 idx = vm_stack_alloc(vm_stack, 3);
  vm_stack->base[idx] = operand;
  vm_stack->base[idx + 1] = key;
  vm_stack->base[idx + 2] = val;
  vm_fun(lv_getfunction(method, 0), frame, 3, idx, 0, 0);
  vm_stack_dealloc(vm_stack, idx);
  return;

normal:
  lhash_set(TBL(operand), key, val);
}
