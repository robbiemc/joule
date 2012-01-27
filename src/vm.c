#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "debug.h"
#include "lhash.h"
#include "luav.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

#define CONST(f, n) ({ assert((n) < (f)->num_consts); (f)->consts[n]; })
#define REG(f, n)                                                             \
  ({                                                                          \
    assert((n) < (f)->max_stack);                                             \
    luav _tmp = stack[n];                                                     \
    lv_gettype(_tmp) == LUPVALUE ? lv_getupvalue(_tmp)->value : _tmp;         \
  })
#define SETREG(f, n, v)                       \
  ({                                          \
    assert((n) < (f)->max_stack);             \
    if (lv_gettype(stack[n]) == LUPVALUE) {   \
      lv_getupvalue(stack[n])->value = v;     \
    } else {                                  \
      stack[n] = v;                           \
    }                                         \
  })

/* TODO: extract out 256 */
#define KREG(func, n) ((n) >= 256 ? CONST(func, (n) - 256) : REG(func, n))
#define UPVALUE(closure, n)                                \
  ({                                                       \
    assert((n) < (closure)->function.lua->num_upvalues);   \
    (closure)->upvalues[n];                                \
  })
#define DECODEFP8(v) (((u32)8 | ((v)&7)) << (((u32)(v)>>3) - 1))

lhash_t lua_globals;

static u32 vm_fun(lclosure_t *c, u32 argc, luav *argv, u32 retc, luav *retv);
static void op_close(u32 upc, luav *upv);

INIT static void vm_setup() {
  lhash_init(&lua_globals);
  lhash_set(&lua_globals, LSTR("_VERSION"), LSTR("Joule 0.0"));
}

DESTROY static void vm_destroy() {
  lhash_free(&lua_globals);
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
  u32 last_ret = 0;
  luav temp, bv, cv;
  double step, d1, d2;
  size_t len;
  lhash_t *ht;
  char *str, *pos;
  lstring_t *lstr;

  luav stack[func->max_stack];

top:
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

      case OP_GETUPVAL:
        temp = UPVALUE(closure, B(code));
        SETREG(func, A(code), lv_getupvalue(temp)->value);
        break;

      case OP_SETUPVAL:
        temp = UPVALUE(closure, B(code));
        lv_getupvalue(temp)->value = REG(func, A(code));
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
        u32 num_args = b == 0 ? last_ret - a - 1 : b - 1;
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
            panic("Bad function type: %d\n", func2->type);
        }

        /* Fill in all the nils */
        if (c != 0) {
          for (i = got; i < c - 1; i++) {
            SETREG(func, i, LUAV_NIL);
          }
        }

        last_ret = a + got;
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

      case OP_TAILCALL:
        a = A(code);
        b = B(code);
        closure = lv_getfunction(REG(func, a));
        assert(closure->type == LUAF_LUA);
        func = closure->function.lua;
        assert(C(code) == 0);
        pc = 0;
        argc = (b == 0 ? last_ret : a + b) - a - 1;
        argv = &stack[a + 1];
        goto top;

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

      case OP_JMP:
        pc += UNBIAS(PAYLOAD(code));
        break;

      case OP_EQ:
        if ((KREG(func, B(code)) == KREG(func, C(code))) != A(code)) {
          pc++;
        }
        break;
      case OP_LT:
        if ((KREG(func, B(code)) < KREG(func, C(code))) != A(code)) {
          pc++;
        }
        break;
      case OP_LE:
        if ((KREG(func, B(code)) <= KREG(func, C(code))) != A(code)) {
          pc++;
        }
        break;

      case OP_TEST:
        temp = REG(func, A(code));
        if (lv_getbool(LUAV_BOOL(temp)) != C(code)) {
          pc++;
        }
        break;

      case OP_TESTSET:
        temp = REG(func, B(code));
        if (lv_getbool(LUAV_BOOL(temp)) != C(code)) {
          SETREG(func, A(code), REG(func, B(code)));
        } else {
          pc++;
        }
        break;

      case OP_LOADBOOL:
        SETREG(func, A(code), lv_bool((u8) B(code)));
        if (C(code)) {
          pc++;
        }
        break;

      case OP_ADD:
        bv = KREG(func, B(code));
        cv = KREG(func, C(code));
        SETREG(func, A(code), lv_number(lv_getnumber(bv) + lv_getnumber(cv)));
        break;

      case OP_SUB:
        bv = KREG(func, B(code));
        cv = KREG(func, C(code));
        SETREG(func, A(code), lv_number(lv_getnumber(bv) - lv_getnumber(cv)));
        break;

      case OP_MUL:
        bv = KREG(func, B(code));
        cv = KREG(func, C(code));
        SETREG(func, A(code), lv_number(lv_getnumber(bv) * lv_getnumber(cv)));
        break;

      case OP_DIV:
        bv = KREG(func, B(code));
        cv = KREG(func, C(code));
        SETREG(func, A(code), lv_number(lv_getnumber(bv) / lv_getnumber(cv)));
        break;

      case OP_MOD:
        bv = KREG(func, B(code));
        cv = KREG(func, C(code));
        SETREG(func, A(code), lv_number(fmod(lv_getnumber(bv),
                                             lv_getnumber(cv))));
        break;

      case OP_POW:
        bv = KREG(func, B(code));
        cv = KREG(func, C(code));
        SETREG(func, A(code), lv_number(pow(lv_getnumber(bv),
                                            lv_getnumber(cv))));
        break;

      case OP_UNM:
        bv = REG(func, B(code));
        SETREG(func, A(code), lv_number(-lv_getnumber(bv)));
        break;

      case OP_NOT:
        bv = REG(func, B(code));
        SETREG(func, A(code), lv_bool(lv_getbool(bv) ^ 1));
        break;

      case OP_LEN:
        bv = REG(func, B(code));
        switch (lv_gettype(bv)) {
          case LSTRING:
            len = lstr_get(lv_getstring(bv))->length - 1;
            SETREG(func, A(code), lv_number((double) len));
            break;
          case LTABLE:
            SETREG(func, A(code), lv_number((double) lv_gettable(bv)->length));
            break;
          default:
            panic("Invalid type for len\n");
        }
        break;

      case OP_NEWTABLE:
        // TODO - We can't currently create a table of a certain size, so we
        //        ignore the size hints. Eventually we should use them.
        ht = xmalloc(sizeof(lhash_t));
        lhash_init(ht);
        SETREG(func, A(code), lv_table(ht));
        break;

      case OP_FORPREP:
        a = A(code);
        SETREG(func, a, lv_number(lv_getnumber(REG(func, a)) -
                                  lv_getnumber(REG(func, a + 2))));
        pc += UNBIAS(PAYLOAD(code));
        break;

      case OP_FORLOOP:
        a = A(code);
        step = lv_getnumber(REG(func, a+2));
        SETREG(func, a, lv_number(lv_getnumber(REG(func, a)) + step));
        d1 = lv_getnumber(REG(func, a));
        d2 = lv_getnumber(REG(func, a + 1));
        if ((step > 0 && d1 <= d2) || (step < 0 && d1 >= d2)) {
          SETREG(func, a + 3, REG(func, a));
          pc += UNBIAS(PAYLOAD(code));
        }
        break;

      case OP_CONCAT:
        len = 0;
        c = C(code);
        for (i = B(code); i <= c; i++) {
          bv = REG(func, i);
          len += lstr_get(lv_getstring(bv))->length-1;
        }
        str = xmalloc(len + 1);
        pos = str;
        for (i = B(code); i <= c; i++) {
          lstr = lstr_get(lv_getstring(REG(func, i)));
          memcpy(pos, lstr->ptr, lstr->length-1);
          pos += lstr->length-1;
        }
        str[len] = '\0';
        SETREG(func, A(code), lv_string(lstr_add(str, len+1, TRUE)));
        break;

      default:
        fprintf(stderr, "Unimplemented opcode: ");
        opcode_dump(stderr, code);
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
