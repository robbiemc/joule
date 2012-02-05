#include <stdlib.h>

#include "config.h"
#include "opcode.h"

char rkbuf1[32];
char rkbuf2[32];

#define RK(v)  rk(rkbuf1, (v))
#define RK2(v) rk(rkbuf2, (v))
char *rk(char *buf, u32 val) {
  if (val >= 256) {
    *buf = 'K';
    val -= 256;
  } else {
    *buf = 'R';
  }
  snprintf(buf+1, 31, "%d", val);
  return buf;
}

void opcode_dump_idx(FILE *out, lfunc_t *func, size_t idx) {
  if (idx > func->num_lines)
    fprintf(out, "??  ");
  else
    fprintf(out, "%2d  ", func->lines[idx]);
  opcode_dump(out, func->instrs[idx]);
}

void opcode_dump(FILE *out, uint32_t code) {
  switch (OP(code)) {
    case OP_MOVE:
      fprintf(out, "MOVE      R%d = R%d", A(code), B(code)); break;
    case OP_LOADK:
      fprintf(out, "LOADK     R%d = K%d", A(code), BX(code)); break;
    case OP_LOADBOOL:
      fprintf(out, "LOADBOOL  R%d = %s; PC += %d", A(code), B(code)?"true":"false", !!C(code));
      break;
    case OP_LOADNIL:
      fprintf(out, "LOADNIL   R%d ... R%d = nil", A(code), B(code)); break;
    case OP_GETUPVAL:
      fprintf(out, "GETUPVAL  R%d = UP[%d]", A(code), B(code)); break;
    case OP_GETGLOBAL:
      fprintf(out, "GETGLOBAL R%d = G[K%d]", A(code), BX(code)); break;
    case OP_GETTABLE:
      fprintf(out, "GETTABLE  R%d = R%d[%s]", A(code), B(code), RK(C(code))); break;
    case OP_SETGLOBAL:
      fprintf(out, "SETGLOBAL G[K%d] = R%d", BX(code), A(code)); break;
    case OP_SETUPVAL:
      fprintf(out, "SETUPVAL  UP[%d] = %d", B(code), A(code)); break;
    case OP_SETTABLE:
      fprintf(out, "SETTABLE  R%d[%s] = %s", A(code), RK(B(code)), RK2(C(code))); break;
    case OP_NEWTABLE:
      fprintf(out, "NEWTABLE  R%d = {}(%02x, %02x)", A(code), B(code), C(code)); break;
    case OP_SELF:
      fprintf(out, "SELF      R%d = R%d; R%d = R%d[%s]", A(code)+1, B(code), A(code),
                                                         B(code), RK(C(code))); break;
    case OP_ADD:
      fprintf(out, "ADD       R%d = %s + %s", A(code), RK(B(code)), RK2(C(code))); break;
    case OP_SUB:
      fprintf(out, "SUB       R%d = %s - %s", A(code), RK(B(code)), RK2(C(code))); break;
    case OP_MUL:
      fprintf(out, "MUL       R%d = %s * %s", A(code), RK(B(code)), RK2(C(code))); break;
    case OP_DIV:
      fprintf(out, "DIV       R%d = %s / %s", A(code), RK(B(code)), RK2(C(code))); break;
    case OP_MOD:
      fprintf(out, "MOD       R%d = %s %% %s", A(code), RK(B(code)), RK2(C(code))); break;
    case OP_POW:
      fprintf(out, "POW       R%d = %s ^^ %s", A(code), RK(B(code)), RK2(C(code))); break;
    case OP_UNM:
      fprintf(out, "UNM       R%d = -R%d", A(code), B(code)); break;
    case OP_NOT:
      fprintf(out, "NOT       R%d = !R%d", A(code), B(code)); break;
    case OP_LEN:
      fprintf(out, "LEN       R%d = len(R%d)", A(code), B(code)); break;
    case OP_CONCAT:
      fprintf(out, "CONCAT    R%d = R%d .. Rn .. R%d", A(code), B(code), C(code)); break;
    case OP_JMP:
      fprintf(out, "JMP       PC += %d", SBX(code)); break;
    case OP_EQ:
      fprintf(out, "EQ        if (%s %s %s) PC++", RK(B(code)), A(code)?"!=":"==",
                                                   RK2(C(code))); break;
    case OP_LT:
      fprintf(out, "LT        if (%s %s %s) PC++", RK(B(code)), A(code)?">=":"<",
                                                   RK2(C(code))); break;
    case OP_LE:
      fprintf(out, "LE        if (%s %s %s) PC++", RK(B(code)), A(code)?">":"<=",
                                                   RK2(C(code))); break;
    case OP_TEST:
      fprintf(out, "TEST      if (R%d == %s) PC++", A(code), !C(code)?"true":"false"); break;
    case OP_TESTSET:
      fprintf(out, "TESTSET   if (R%d == %s) PC++ else R%d = R%d", A(code),
                                            !C(code)?"true":"false", A(code), B(code)); break;
    case OP_CALL:
      fprintf(out, "CALL      R%d, ..., R%d = R%d(R%d, ..., R%d)", A(code), A(code)+C(code)-2,
                                            A(code), A(code)+1, A(code)+B(code)-1); break;
    case OP_TAILCALL:
      fprintf(out, "TAILCALL  ret R%d(R%d, ...,  R%d)", A(code), A(code)+1, A(code)+B(code)-1);
      break;
    case OP_RETURN:
      fprintf(out, "RETURN    R%d, ..., R%d", A(code), A(code)+B(code)-2); break;
    case OP_FORLOOP:
      fprintf(out, "FORPREP   %d %d", A(code), SBX(code)); break;
    case OP_FORPREP:
      fprintf(out, "FORLOOP   %d %d", A(code), SBX(code)); break;
    case OP_TFORLOOP:
      fprintf(out, "TFORLOOP  %d %d", A(code), C(code)); break;
    case OP_SETLIST:
      fprintf(out, "SETLIST   %d %d %d", A(code), B(code), C(code)); break;
    case OP_CLOSE:
      fprintf(out, "CLOSE     %d", A(code)); break;
    case OP_CLOSURE:
      fprintf(out, "CLOSURE   R%d = F%d", A(code), BX(code)); break;
    case OP_VARARG:
      fprintf(out, "VARARG    %d %d", A(code), B(code)); break;

    default:
      printf("Bad opcode: 0x%08x", code);
      exit(1);
  }
}
