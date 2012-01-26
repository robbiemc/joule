#include <assert.h>
#include <limits.h>

#include "debug.h"
#include "lhash.h"
#include "luav.h"
#include "opcode.h"
#include "vm.h"

#define CONST(f, n) ({ assert((n) < (f)->num_consts); (f)->consts[n]; })
#define REG(f, n)                                                             \
  ({                                                                          \
    assert((n) < (f)->max_stack);                                             \
    luav _tmp = stack[n];                                                     \
    lv_gettype(_tmp) == LUPVALUE ? lv_getupvalue(_tmp)->value : _tmp;         \
  })
#define SETREG(f, n, v)                \
  ({                                   \
    assert((n) < (f)->max_stack);      \
    if (lv_gettype(v) == LUPVALUE) {   \
      lv_getupvalue(v)->value = v;     \
    } else {                           \
      stack[n] = v;                    \
    }                                  \
  })

/* TODO: extract out 256 */
#define KREG(func, n) ((n) >= 256 ? CONST(func, (n) - 256) : REG(func, n))
#define UPVALUE(closure, n)                                \
  ({                                                       \
    assert((n) < (closure)->function.lua->num_upvalues);   \
    (closure)->upvalues[n];                                \
  })

lhash_t lua_globals;

static u32 vm_fun(lclosure_t *c, u32 argc, luav *argv, u32 retc, luav *retv);
static void op_close(u32 upc, luav *upv);

INIT static void vm_setup() {
  printf("vm_setup\n");
  lhash_init(&lua_globals);
  lhash_set(&lua_globals, LSTR("_VERSION"), LSTR("Joule 0.0"));
}

void vm_run(lfunc_t *func) {
  lclosure_t closure = {.function.lua = func, .type = LUAF_LUA};
  assert(func->num_upvalues == 0);
  vm_fun(&closure, 0, NULL, 0, NULL);
}

static u32 vm_fun(lclosure_t *closure, u32 argc, luav *argv,
                                       u32 retc, luav *retv) {
  lfunc_t *func = closure->function.lua;
  u32 pc = 0;
  u32 i, a, b, c, bx, limit;
  luav temp;

  luav stack[func->max_stack];
  for (i = 0; i < func->max_stack; i++) {
    stack[i] = i < argc ? argv[i] : LUAV_NIL;
  }

  while (1) {
    assert(pc < func->num_instrs);
    u32 code = func->instrs[pc++];

    switch (OP(code)) {
      case OP_GETGLOBAL:
        temp = CONST(func, PAYLOAD(code));
        assert(lv_gettype(temp) == LSTRING);
        SETREG(func, A(code), lhash_get(&lua_globals, temp));
        break;

      case OP_SETGLOBAL:
        temp = CONST(func, PAYLOAD(code));
        assert(lv_gettype(temp) == LSTRING);
        lhash_set(&lua_globals, temp, REG(func, A(code)));
        break;

      case OP_GETTABLE:
        temp = KREG(func, C(code));
        temp = lhash_get(lv_gettable(REG(func, B(code))), temp);
        SETREG(func, A(code), temp);
        break;

      case OP_SETTABLE:
        lhash_set(lv_gettable(REG(func, A(code))),
                  KREG(func, B(code)), KREG(func, C(code)));
        break;

      case OP_LOADK:
        SETREG(func, A(code), CONST(func, PAYLOAD(code)));
        break;

      case OP_LOADNIL:
        for (i = A(code); i <= B(code); i++) {
          SETREG(func, i, LUAV_NIL);
        }
        break;

      case OP_MOVE:
        SETREG(func, A(code), REG(func, B(code)));
        break;

      case OP_CALL: {
        a = A(code);
        lclosure_t *func2 = lv_getfunction(REG(func, a));
        b = B(code);
        u32 num_args = b == 0 ? func->max_stack - a + 1 : b - 1;
        c = C(code);
        u32 want_ret = c == 0 ? UINT_MAX : c - 1;
        u32 got;

        switch (func2->type) {
          case LUAF_LUA:
            got = vm_fun(func2, num_args, &stack[a + 1], want_ret, &stack[a]);
            break;
          case LUAF_C_VARARG:
            got = func2->function.vararg(num_args, &stack[a + 1],
                                         want_ret, &stack[a]);
            break;
          case LUAF_C_1ARG:
            got = 1;
            temp = func2->function.onearg(num_args > 0 ? stack[a + 1]
                                                       : LUAV_NIL);
            if (want_ret > 0) {
              stack[a] = temp;
            }
            break;
          case LUAF_C_2ARG:
            got = 1;
            temp = func2->function.twoarg(
                              num_args > 0 ? stack[a + 1] : LUAV_NIL,
                              num_args > 1 ? stack[a + 2] : LUAV_NIL);
            if (want_ret > 0) {
              stack[a] = temp;
            }
            break;

          default:
            printf("Bad function type: %d\n", func2->type);
            abort();
        }

        /* Fill in all the nils */
        if (c != 0) {
          for (i = got; i < c - 1; i++) {
            SETREG(func, i, LUAV_NIL);
          }
        }
        break;
      }

      case OP_RETURN:
        a = A(code);
        b = B(code);
        if (b == 0) {
          limit = func->max_stack - a;
        } else {
          limit = b - 1;
        }
        for (i = 0; i < limit && i < retc; i++) {
          assert(a + i < func->max_stack);
          retv[i] = stack[a + i];
        }
        op_close(func->max_stack, stack);
        return i;

      case OP_CLOSURE: {
        bx = PAYLOAD(code);
        assert(bx < func->num_funcs);
        lfunc_t *function = &func->funcs[bx];
        lclosure_t *closure2 = xmalloc(CLOSURE_SIZE(function->num_upvalues));
        closure2->type = LUAF_LUA;
        closure2->function.lua = function;

        for (i = 0; i < function->num_upvalues; i++) {
          u32 pseudo = func->instrs[pc++];
          luav upvalue;
          if (OP(pseudo) == OP_MOVE) {
            /* Can't use the REG macro because we don't want to duplicate
               upvalues. If the register on the stack is an upvalue, we want to
               use the same upvalue... */
            assert(B(pseudo) < func->max_stack);
            temp = stack[B(pseudo)];
            if (lv_gettype(temp) == LUPVALUE) {
              upvalue = temp;
            } else {
              /* If the stack register is not an upvalue, we need to promote it
                 to an upvalue, thereby scribbling over our stack variable so
                 it's now considered an upvalue */
              upvalue_t *ptr = xmalloc(sizeof(upvalue_t));
              ptr->refcnt = 1; /* one for us, one for them added later */
              ptr->value = temp;
              upvalue = lv_upvalue(ptr);
              stack[B(pseudo)] = upvalue;
            }
          } else {
            upvalue = UPVALUE(closure, B(pseudo));
          }

          /* The allocated closure needs a reference to the upvalue */
          lv_getupvalue(upvalue)->refcnt++;
          closure2->upvalues[i] = upvalue;
        }

        SETREG(func, A(code), lv_function(closure2));
        break;
      }

      case OP_CLOSE:
        op_close(func->max_stack - A(code), &stack[A(code)]);
        break;

      default:
        printf("Unimplemented opcode: ");
        opcode_dump(stdout, code);
        abort();
    }
  }
}

static void op_close(u32 upc, luav *upv) {
  u32 i;
  for (i = 0; i < upc; i++) {
    if (lv_gettype(upv[i]) == LUPVALUE) {
      upvalue_t *upvalue = lv_getupvalue(upv[i]);
      if (--upvalue->refcnt == 0) {
        upv[i] = upvalue->value;
        free(upvalue);
      }
    }
  }
}
