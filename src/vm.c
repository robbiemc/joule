#include <assert.h>
#include <limits.h>

#include "debug.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "opcode.h"
#include "vm.h"

#define CONST(f, n) ({ assert((n) < (f)->num_consts); (f)->consts[n]; })
#define REG(f, n) ({ assert((n) < (f)->max_stack); stack[n]; })
#define SETREG(f, n, v) ({ assert((n) < (f)->max_stack); stack[n] = v; })
/* TODO: extract out 256 */
#define KREG(func, n) ((n) >= 256 ? CONST(func, (n) - 256) : REG(func, n))

lhash_t globals;
lhash_t io;

static u32 io_print(u32 argc, luav *argv, u32 retc, luav *retv);
static u32 vm_fun(lfunc_t *func, u32 argc, luav *argv, u32 retc, luav *retv);

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
  vm_fun(func, 0, NULL, 0, NULL);
}

static u32 vm_fun(lfunc_t *func, u32 argc, luav *argv, u32 retc, luav *retv) {
  u32 pc = 0;
  u32 i, a, b, c, bx, limit;
  luav temp;
  lfunc_t *func2;

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

      case OP_CALL: {
        func2 = lv_getfunction(REG(func, A(code)));
        b = B(code);
        u32 num_args = b == 0 ? func->max_stack - a + 1 : b - 1;
        c = C(code);
        u32 want_ret = c == 0 ? UINT_MAX : c - 1;
        assert(c != 0 && "If this trips, write some real code");
        u32 got;
        if (func2->cfunc) {
          got = func2->cfunc(num_args, &stack[a + 1], want_ret, &stack[a]);
        } else {
          got = vm_fun(func2, num_args, &stack[a + 1],
                                 want_ret, &stack[a]);
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

      case OP_CLOSURE:
        a = A(code);
        bx = PAYLOAD(code);
        assert(bx < func->num_funcs);
        func2 = &func->funcs[bx];
        SETREG(func, A(code), lv_function(func2));
        assert(func2->num_upvalues == 0);
        break;

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
