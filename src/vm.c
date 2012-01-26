#include <assert.h>
#include <limits.h>

#include "debug.h"
#include "lhash.h"
#include "lstring.h"
#include "luav.h"
#include "opcode.h"
#include "vm.h"

lhash_t globals;
lhash_t io;

static u32 io_print(u32 argc, luav *argv, u32 retc, luav *retv);
static u32 vm_fun(lfunc_t *fun, u32 argc, luav *argv, u32 retc, luav *retv);

LUA_FUNCTION(io_print);

static void vm_setup() {
  lhash_init(&globals);
  lhash_init(&io);

  lhash_set(&globals, LSTR("io"), lv_table(&io));
  lhash_set(&globals, LSTR("_VERSION"), LSTR("Joule 0.0"));
  lhash_set(&io, LSTR("write"), lv_function(&io_print_f));
}

void vm_run(lfunc_t *fun) {
  vm_setup();
  vm_fun(fun, 0, NULL, 0, NULL);
}

static u32 vm_fun(lfunc_t *fun, u32 argc, luav *argv, u32 retc, luav *retv) {
  u32 pc = 0;
  u32 i, a, b, c, bx, limit;
  luav temp, temp2;
  lfunc_t *fun2;

  luav stack[fun->max_stack];
  for (i = 0; i < (u32) argc; i++) {
    stack[i] = argv[i];
  }

  while (1) {
    assert(pc < fun->num_instrs);
    u32 code = fun->instrs[pc++];
    switch (OP(code)) {
      case OP_GETGLOBAL:
        bx = PAYLOAD(code);
        assert(bx < fun->num_consts);
        temp = fun->consts[bx];
        assert(lv_gettype(temp) == LSTRING);
        temp = lhash_get(&globals, temp);
        assert(A(code) < fun->max_stack);
        stack[A(code)] = temp;
        break;

      case OP_SETGLOBAL:
        bx = PAYLOAD(code);
        assert(bx < fun->num_consts);
        temp = fun->consts[bx];
        assert(lv_gettype(temp) == LSTRING);
        assert(A(code) < fun->max_stack);
        lhash_set(&globals, temp, stack[A(code)]);
        break;

      case OP_GETTABLE:
        c = C(code);
        if (c >= 256) {
          /* TODO: extract out 256 */
          assert((c - 256) < fun->max_stack);
          temp = fun->consts[c - 256];
        } else {
          assert(c < fun->max_stack);
          temp = stack[c];
        }
        assert(B(code) < fun->max_stack);
        temp2 = stack[B(code)];
        temp = lhash_get(lv_gettable(temp2), temp);
        assert(A(code) < fun->max_stack);
        stack[A(code)] = temp;
        break;

      case OP_LOADK:
        bx = PAYLOAD(code);
        assert(bx < fun->num_consts);
        assert(A(code) < fun->max_stack);
        stack[A(code)] = fun->consts[bx];
        break;

      case OP_CALL: {
        a = A(code);
        assert(a < fun->max_stack);
        fun2 = lv_getfunction(stack[a]);
        b = B(code);
        u32 num_args = b == 0 ? fun->max_stack - a + 1 : b - 1;
        c = C(code);
        u32 want_ret = c == 0 ? UINT_MAX : c - 1;
        assert(c != 0 && "If this trips, write some real code");
        u32 got;
        if (fun2->cfunc) {
          got = fun2->cfunc(num_args, &stack[a + 1], want_ret, &stack[a]);
        } else {
          got = vm_fun(fun2, num_args, &stack[a + 1],
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
          limit = fun->max_stack - a;
        } else {
          limit = b - 1;
        }
        for (i = 0; i < limit && i < retc; i++) {
          assert(a + i < fun->max_stack);
          retv[i] = stack[a + i];
        }
        return i;

      case OP_CLOSURE:
        a = A(code);
        bx = PAYLOAD(code);
        assert(bx < fun->num_funcs);
        temp = lv_function(&fun->funcs[bx]);
        assert(a < fun->max_stack);
        stack[a] = temp;
        assert(fun->funcs[bx].num_upvalues == 0);
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
