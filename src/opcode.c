#include <stdlib.h>

#include "opcode.h"

void opcode_dump(FILE *out, uint32_t code) {
  switch (OP(code)) {
    case OP_MOVE:
      fprintf(out, "MOVE %d %d\n", A(code), B(code)); break;
    case OP_LOADK:
      fprintf(out, "LOADK %d %d\n", A(code), B(code)); break;
    case OP_LOADBOOL:
      fprintf(out, "LOADNIL %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_LOADNIL:
      fprintf(out, "LOADNIL %d %d\n", A(code), B(code)); break;
    case OP_GETUPVAL:
      fprintf(out, "GETUPVAL %d %d\n", A(code), B(code)); break;
    case OP_GETGLOBAL:
      fprintf(out, "GETGLOBAL %d %d\n", A(code), B(code)); break;
    case OP_GETTABLE:
      fprintf(out, "GETTABLE %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_SETGLOBAL:
      fprintf(out, "SETGLOBAL %d %d\n", A(code), B(code)); break;
    case OP_SETUPVAL:
      fprintf(out, "SETUPVAL %d %d\n", A(code), B(code)); break;
    case OP_SETTABLE:
      fprintf(out, "SETTABLE %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_NEWTABLE:
      fprintf(out, "NEWTABLE %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_SELF:
      fprintf(out, "SELF %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_ADD:
      fprintf(out, "ADD %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_SUB:
      fprintf(out, "SUB %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_MUL:
      fprintf(out, "MUL %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_DIV:
      fprintf(out, "DIV %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_MOD:
      fprintf(out, "MOD %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_POW:
      fprintf(out, "POW %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_UNM:
      fprintf(out, "UNM %d %d\n", A(code), B(code)); break;
    case OP_NOT:
      fprintf(out, "NOT %d %d\n", A(code), B(code)); break;
    case OP_LEN:
      fprintf(out, "LEN %d %d\n", A(code), B(code)); break;
    case OP_CONCAT:
      fprintf(out, "CONCAT %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_JMP:
      fprintf(out, "JMP %d\n", PAYLOAD(code)); break;
    case OP_EQ:
      fprintf(out, "EQ %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_LT:
      fprintf(out, "LT %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_LE:
      fprintf(out, "LE %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_TEST:
      fprintf(out, "TEST %d %d\n", A(code), B(code)); break;
    case OP_TESTSET:
      fprintf(out, "TESTSET %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_CALL:
      fprintf(out, "CALL %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_TAILCALL:
      fprintf(out, "TAILCALL %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_RETURN:
      fprintf(out, "RETURN %d %d\n", A(code), B(code)); break;
    case OP_FORLOOP:
      fprintf(out, "FORPREP %d %d\n", A(code), PAYLOAD(code)); break;
    case OP_FORPREP:
      fprintf(out, "FORLOOP %d %d\n", A(code), PAYLOAD(code)); break;
    case OP_TFORLOOP:
      fprintf(out, "TFORLOOP %d %d\n", A(code), C(code)); break;
    case OP_SETLIST:
      fprintf(out, "SETLIST %d %d %d\n", A(code), B(code), C(code)); break;
    case OP_CLOSE:
      fprintf(out, "CLOSE %d\n", A(code)); break;
    case OP_CLOSURE:
      fprintf(out, "CLOSURE %d %d\n", A(code), PAYLOAD(code)); break;
    case OP_VARARG:
      fprintf(out, "VARARG %d %d\n", A(code), B(code)); break;

    default:
      printf("Bad opcode: 0x%08x", code);
      exit(1);
  }
}
