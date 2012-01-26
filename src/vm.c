#include <assert.h>
#include <limits.h>

#include "debug.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "opcode.h"
#include "vm.h"

#define CONST(f, n) ({ assert((n) < (f)->num_consts); (f)->consts[n]; })
#define REG(f, n)                                                             \
  ({                                                                          \
    assert((n) < (f)->max_stack);                                             \
    lv_gettype(stack[n]) == LUPVALUE ? *lv_getupvalue(stack[n]) : stack[n];   \
  })
#define SETREG(f, n, v)                \
  ({                                   \
    assert((n) < (f)->max_stack);      \
    if (lv_gettype(v) == LUPVALUE) {   \
      *lv_getupvalue(v) = v;           \
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

lhash_t globals;
lhash_t io;

static u32 io_print(u32 argc, luav *argv, u32 retc, luav *retv);
static u32 vm_fun(lclosure_t *c, u32 argc, luav *argv, u32 retc, luav *retv);

LUA_FUNCTION(io_print);

static void vm_setup() {
  lhash_init(&globals);
  lhash_init(&io);

  lhash_set(&globals, LSTR("io"), lv_table(&io));
  lhash_set(&globals, LSTR("_VERSION"), LSTR("Joule 0.0"));
  lhash_set(&io, LSTR("write"), lv_function(&io_print_f));
}

void vm_run(lfunc_t *func) {
  vm_setup();
  lclosure_t closure = {.function.lua = func};
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
        SETREG(func, A(code), lhash_get(&globals, temp));
        break;

      case OP_SETGLOBAL:
        temp = CONST(func, PAYLOAD(code));
        assert(lv_gettype(temp) == LSTRING);
        lhash_set(&globals, temp, REG(func, A(code)));
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
        assert(c != 0 && "If this trips, write some real code");
        u32 got;
        if (func2->type == LUAF_CFUNCTION) {
          got = func2->function.c(num_args, &stack[a + 1], want_ret, &stack[a]);
        } else {
          got = vm_fun(func2, num_args, &stack[a + 1], want_ret, &stack[a]);
        }
        /* Fill in all the nils */
        for (i = got; i < c - 1; i++) {
          stack[i] = LUAV_NIL;
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
        return i;

      case OP_CLOSURE: {
        bx = PAYLOAD(code);
        assert(bx < func->num_funcs);
        lfunc_t *function = &func->funcs[bx];
        lclosure_t *closure2 = xmalloc(CLOSURE_SIZE(function->num_upvalues));
        closure2->type = LUAF_LFUNCTION;
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
              luav *ptr = xmalloc(sizeof(luav)); /* TODO: better way?!? */
              *ptr = temp;
              upvalue = lv_upvalue(ptr);
              stack[B(pseudo)] = upvalue;
            }
          } else {
            /* Not much to do here... */
            upvalue = UPVALUE(closure, B(pseudo));
          }

          closure2->upvalues[i] = upvalue;
        }

        SETREG(func, A(code), lv_function(closure2));
        break;
      }

      default:
        printf("Unimplemented opcode: ");
        opcode_dump(stdout, code);
        abort();
    }
  }
}

static u32 io_print(u32 argc, luav *argv, u32 retc, luav *retv) {
  while (argc-- > 0) {
    dbg_dump_luav(stdout, *argv++);
  }
  if (retc > 0) {
    *retv = lv_bool(1);
  }
  return !!retc;
}
