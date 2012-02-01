#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "error.h"
#include "lhash.h"
#include "luav.h"
#include "meta.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

/* TODO: is_vararg requires more stack, exactly how much more? */
#define STACK_SIZE(f) ((u32) (f)->max_stack + 10)
#define CONST(f, n) ({ assert((n) < (f)->num_consts); (f)->consts[n]; })
#define REG(f, n)                                                             \
  ({                                                                          \
    assert((n) < STACK_SIZE(f));                                              \
    luav _tmp = stack[n];                                                     \
    lv_isupvalue(_tmp) ? lv_getupvalue(_tmp)->value : _tmp;                   \
  })
#define SETREG(f, n, v)                       \
  ({                                          \
    assert((n) < STACK_SIZE(f));              \
    if (lv_isupvalue(stack[n])) {             \
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
#define ARG(i) (argc > (i) ? argv[(i)] : LUAV_NIL)
#define DECODEFP8(v) (((u32)8 | ((v)&7)) << (((u32)(v)>>3) - 1))

lhash_t lua_globals;
lframe_t *vm_running = NULL;

static void op_close(u32 upc, luav *upv);

INIT static void vm_setup() {
  lhash_init(&lua_globals);
  lhash_set(&lua_globals, LSTR("_VERSION"), LSTR("Joule 0.0"));
  lhash_set(&lua_globals, LSTR("_G"), lv_table(&lua_globals));
}

DESTROY static void vm_destroy() {
  lhash_free(&lua_globals);
}

void vm_run(lfunc_t *func) {
  lclosure_t closure = {.function.lua = func, .type = LUAF_LUA};
  assert(func->num_upvalues == 0);
  vm_fun(&closure, NULL, 0, NULL, 0, NULL);
}

u32 vm_fun(lclosure_t *closure, lframe_t *parent,
           u32 argc, luav *argv, u32 retc, luav *retv) {
  lframe_t frame;

  frame.caller  = parent;
  frame.closure = closure;
  frame.pc      = 0;
  vm_running    = &frame;

  // handle c functions
  if (closure->type != LUAF_LUA) {
    return closure->function.c->f(argc, argv, retc, retv);
  }

  // it's a lua function
  u32 i, a, b, c, bx, limit;
  u32 last_ret = 0;
  lfunc_t *func = closure->function.lua;
  luav temp;

  luav stack[STACK_SIZE(func)];
  memcpy(stack, argv, sizeof(luav) * argc);
  for (i = argc; i < STACK_SIZE(func); i++) {
    stack[i] = LUAV_NIL;
  }

  for (frame.pc = 0; frame.pc < func->num_instrs;) {
    u32 code = func->instrs[frame.pc++];

    switch (OP(code)) {
      case OP_GETGLOBAL:
        temp = CONST(func, PAYLOAD(code));
        assert(lv_isstring(temp));
        SETREG(func, A(code), lhash_get(&lua_globals, temp));
        break;

      case OP_SETGLOBAL:
        temp = CONST(func, PAYLOAD(code));
        assert(lv_isstring(temp));
        lhash_set(&lua_globals, temp, REG(func, A(code)));
        break;

      case OP_GETTABLE:
        temp = KREG(func, C(code));
        temp = lhash_get(lv_gettable(REG(func, B(code)), 0), temp);
        SETREG(func, A(code), temp);
        break;

      case OP_SETTABLE:
        lhash_set(lv_gettable(REG(func, A(code)), 0),
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
        a = A(code); b = B(code); c = C(code);
        lclosure_t *closure2 = lv_getfunction(REG(func, a), 0);
        u32 num_args = b == 0 ? last_ret - a - 1 : b - 1;
        u32 want_ret = c == 0 ? UINT_MAX : c - 1;

        u32 got = vm_fun(closure2, &frame, num_args, &stack[a + 1],
                                           want_ret, &stack[a]);
        // fill in the nils
        if (c != 0) {
          for (i = got; i < c - 1; i++) {
            SETREG(func, a + i, LUAV_NIL);
          }
        }
        last_ret = a + got;
        break;
      }

      case OP_RETURN:
        a = A(code);
        b = B(code);
        if (b == 0) {
          limit = STACK_SIZE(func) - a;
        } else {
          limit = b - 1;
        }
        for (i = 0; i < limit && i < retc; i++) {
          retv[i] = REG(func, a + i);
        }
        op_close(STACK_SIZE(func), stack);
        return i;

      case OP_TAILCALL:
        a = A(code);
        b = B(code);
        closure = lv_getfunction(REG(func, a), 0);
        assert(C(code) == 0);
        argc = (b == 0 ? last_ret : a + b) - a - 1;
        argv = &stack[a + 1];
        /* TODO: this isn't actually a tail call... */
        return vm_fun(closure, &frame, argc, argv, retc, retv);

      case OP_CLOSURE: {
        bx = PAYLOAD(code);
        assert(bx < func->num_funcs);
        lfunc_t *function = &func->funcs[bx];
        lclosure_t *closure2 = xmalloc(CLOSURE_SIZE(function->num_upvalues));
        closure2->type = LUAF_LUA;
        closure2->function.lua = function;

        for (i = 0; i < function->num_upvalues; i++) {
          u32 pseudo = func->instrs[frame.pc++];
          luav upvalue;
          if (OP(pseudo) == OP_MOVE) {
            /* Can't use the REG macro because we don't want to duplicate
               upvalues. If the register on the stack is an upvalue, we want to
               use the same upvalue... */
            assert(B(pseudo) < STACK_SIZE(func));
            temp = stack[B(pseudo)];
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
        op_close(STACK_SIZE(func) - A(code), &stack[A(code)]);
        break;

      case OP_JMP:
        frame.pc += UNBIAS(PAYLOAD(code));
        break;

      case OP_EQ: {
        luav bv = KREG(func, B(code));
        luav cv = KREG(func, C(code));
        if (bv == LUAV_NIL) {
          if ((cv == LUAV_NIL) != A(code)) frame.pc++;
        } else if (cv == LUAV_NIL) {
          if ((bv == LUAV_NIL) != A(code)) frame.pc++;
        } else if ((lv_compare(bv, cv) == 0) != A(code)) {
          frame.pc++;
        }
        break;
      }
      case OP_LT: {
        int cmp = lv_compare(KREG(func, B(code)), KREG(func, C(code)));
        if ((cmp < 0) != A(code)) {
          frame.pc++;
        }
        break;
      }
      case OP_LE: {
        int cmp = lv_compare(KREG(func, B(code)), KREG(func, C(code)));
        if ((cmp <= 0) != A(code)) {
          frame.pc++;
        }
        break;
      }

      case OP_TEST:
        temp = REG(func, A(code));
        if (lv_getbool(temp, 0) != C(code)) {
          frame.pc++;
        }
        break;

      case OP_TESTSET:
        temp = REG(func, B(code));
        if (lv_getbool(temp, 0) != C(code)) {
          SETREG(func, A(code), temp);
        } else {
          frame.pc++;
        }
        break;

      case OP_LOADBOOL:
        SETREG(func, A(code), lv_bool((u8) B(code)));
        if (C(code)) {
          frame.pc++;
        }
        break;

      case OP_ADD: {
        // check for regular addition
        luav bv = KREG(func, B(code));
        luav cv = KREG(func, C(code));
        if (lv_isnumber(bv) && lv_isnumber(cv)) {
          SETREG(func, A(code), lv_number(lv_cvt(bv) + lv_cvt(cv)));
          break;
        }
        // check for metamethods
        a = A(code);
        luav meth;
        lhash_t *meta;
        if (lv_istable(bv) && (meta = lv_gettable(bv,0)->metatable) != NULL) {
          if ((meth = meta->metamethods[META_ADD]) != LUAV_NIL) {
            luav v[2] = {bv, cv};
            u32 got = vm_fun(lv_getfunction(meth,0), &frame, 2, v, 1, &stack[a]);
            if (got == 0)
              SETREG(func, a, LUAV_NIL);
            break;
          }
        }
        if (lv_istable(cv) && (meta = lv_gettable(cv,1)->metatable) != NULL) {
          if ((meth = meta->metamethods[META_ADD]) != LUAV_NIL) {
            luav v[2] = {bv, cv};
            u32 got = vm_fun(lv_getfunction(meth,1), &frame, 2, v, 1, &stack[a]);
            if (got == 0)
              SETREG(func, a, LUAV_NIL);
            break;
          }
        }
        // check for string coersion
        double bd = lv_castnumber(bv, 0);
        double cd = lv_castnumber(cv, 1);
        SETREG(func, a, lv_number(bd + cd));
        break;
      }

      case OP_SUB: {
        double bv = lv_castnumber(KREG(func, B(code)), 0);
        double cv = lv_castnumber(KREG(func, C(code)), 0);
        SETREG(func, A(code), lv_number(bv - cv));
        break;
      }

      case OP_MUL: {
        double bv = lv_castnumber(KREG(func, B(code)), 0);
        double cv = lv_castnumber(KREG(func, C(code)), 0);
        SETREG(func, A(code), lv_number(bv * cv));
        break;
      }

      case OP_DIV: {
        double bv = lv_castnumber(KREG(func, B(code)), 0);
        double cv = lv_castnumber(KREG(func, C(code)), 0);
        SETREG(func, A(code), lv_number(bv / cv));
        break;
      }

      case OP_MOD: {
        double bv = lv_castnumber(KREG(func, B(code)), 0);
        double cv = lv_castnumber(KREG(func, C(code)), 0);
        SETREG(func, A(code), lv_number(fmod(bv, cv)));
        break;
      }

      case OP_POW: {
        double bv = lv_castnumber(KREG(func, B(code)), 0);
        double cv = lv_castnumber(KREG(func, C(code)), 0);
        SETREG(func, A(code), lv_number(pow(bv, cv)));
        break;
      }

      case OP_UNM: {
        double bv = lv_castnumber(REG(func, B(code)), 0);
        SETREG(func, A(code), lv_number(-bv));
        break;
      }

      case OP_NOT: {
        u8 bv = lv_getbool(REG(func, B(code)), 0);
        SETREG(func, A(code), lv_bool(bv ^ 1));
        break;
      }

      case OP_LEN: {
        luav bv = REG(func, B(code));
        switch (lv_gettype(bv)) {
          case LSTRING: {
            size_t len = lv_caststring(bv, 0)->length;
            SETREG(func, A(code), lv_number((double) len));
            break;
          }
          case LTABLE: {
            size_t len = lv_gettable(bv, 0)->length;
            SETREG(func, A(code), lv_number((double) len));
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
        SETREG(func, A(code), lv_table(ht));
        break;
      }

      case OP_FORPREP:
        a = A(code);
        SETREG(func, a, lv_number(lv_castnumber(REG(func, a), 0) -
                                  lv_castnumber(REG(func, a + 2), 0)));
        frame.pc += UNBIAS(PAYLOAD(code));
        break;

      case OP_FORLOOP: {
        a = A(code);
        double step = lv_castnumber(REG(func, a+2), 0);
        SETREG(func, a, lv_number(lv_castnumber(REG(func, a), 0) + step));
        double d1 = lv_castnumber(REG(func, a), 0);
        double d2 = lv_castnumber(REG(func, a + 1), 0);
        if ((step > 0 && d1 <= d2) || (step < 0 && d1 >= d2)) {
          SETREG(func, a + 3, REG(func, a));
          frame.pc += UNBIAS(PAYLOAD(code));
        }
        break;
      }

      case OP_CONCAT: {
        size_t len = 0, cap = LUAV_INIT_STRING;
        c = C(code);
        char *str = xmalloc(cap);
        for (i = B(code); i <= c; i++) {
          lstring_t *lstr = lv_caststring(REG(func, i), 0);
          while (lstr->length + len + 1 >= cap) {
            cap *= 2;
            str = xrealloc(str, cap);
          }
          memcpy(str + len, lstr->ptr, lstr->length);
          len += lstr->length;
        }
        str[len] = 0;
        SETREG(func, A(code), lv_string(lstr_add(str, len, TRUE)));
        break;
      }

      case OP_SETLIST: {
        b = B(code);
        a = A(code);
        c = C(code);
        if (c == 0) {
          c = func->instrs[frame.pc++];
        }
        lhash_t *hash = lv_gettable(REG(func, a), 0);
        for (i = 1; i <= b; i++) {
          lhash_set(hash, lv_number((c - 1) * LFIELDS_PER_FLUSH + i),
                          REG(func, a + i));
        }
        break;
      }

      case OP_VARARG: {
        a = A(code);
        b = B(code);
        u32 limit = b > 0 ? b - 1 : argc;
        for (i = 0; i < limit && i < argc; i++) {
          SETREG(func, a + i, argv[i]);
        }
        for (; i < limit; i++) {
          SETREG(func, a + i, LUAV_NIL);
        }
        last_ret = a + i;
        break;
      }

      case OP_SELF:
        SETREG(func, A(code) + 1, REG(func, B(code)));
        temp = KREG(func, C(code));
        temp = lhash_get(lv_gettable(REG(func, B(code)), 0), temp);
        SETREG(func, A(code), temp);
        break;

      case OP_TFORLOOP:
        a = A(code); c = C(code);
        lclosure_t *closure2 = lv_getfunction(REG(func, a), 0);
        u32 got = vm_fun(closure2, &frame, 2, &stack[a + 1],
                                   (u32) REG(func, c), &stack[a + 3]);
        temp = REG(func, a + 3);
        if (got == 0 || temp == LUAV_NIL) {
          frame.pc++;
        } else {
          SETREG(func, a + 2, temp);
        }
        // fill in the nils
        if (c != 0) {
          for (i = got; i < c; i++) {
            SETREG(func, a + 3 + i, LUAV_NIL);
          }
        }
        break;

      default:
        fprintf(stderr, "Unimplemented opcode: ");
        opcode_dump(stderr, code);
        abort();
    }

    /* TODO: this is a bad fix */
    vm_running = &frame;
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
