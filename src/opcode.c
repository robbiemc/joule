#include <stdlib.h>

#include "opcode.h"

#define OP(opc)       ((opc) & 0x3f)
#define A(opc)        (((opc) >> 6) & 0xff) 
#define B(opc)        (((opc) >> 23) & 0x1ff) 
#define C(opc)        (((opc) >> 14) & 0x1ff) 
#define PAYLOAD(opc)  (((opc) >> 14) & 0x3ffff) 

void opcode_dump(FILE *out, uint32_t code) {
  switch (OP(code)) {
    case 0: // MOVE
      fprintf(out, "MOVE %d %d\n", A(code), B(code)); break;
    case 1: // LOADK
      fprintf(out, "LOADK %d %d\n", A(code), B(code)); break;
    case 2: // LOADBOOL
      fprintf(out, "LOADNIL %d %d %d\n", A(code), B(code), C(code)); break;
    case 3: // LOADNIL
      fprintf(out, "LOADNIL %d %d\n", A(code), B(code)); break;
    case 4: // GETUPVAL
      fprintf(out, "GETUPVAL %d %d\n", A(code), B(code)); break;
    case 5: // GETGLOBAL
      fprintf(out, "GETGLOBAL %d %d\n", A(code), B(code)); break;
    case 6: // GETTABLE
      fprintf(out, "GETTABLE %d %d %d\n", A(code), B(code), C(code)); break;
    case 7: // SETGLOBAL
      fprintf(out, "SETGLOBAL %d %d\n", A(code), B(code)); break;
    case 8: // SETUPVAL
      fprintf(out, "SETUPVAL %d %d\n", A(code), B(code)); break;
    case 9: // SETTABLE
      fprintf(out, "SETTABLE %d %d %d\n", A(code), B(code), C(code)); break;
    case 10: // NEWTABLE
      fprintf(out, "NEWTABLE %d %d %d\n", A(code), B(code), C(code)); break;
    case 11: // SELF
      fprintf(out, "SELF %d %d %d\n", A(code), B(code), C(code)); break;
    case 12: // ADD
      fprintf(out, "ADD %d %d %d\n", A(code), B(code), C(code)); break;
    case 13: // SUB
      fprintf(out, "SUB %d %d %d\n", A(code), B(code), C(code)); break;
    case 14: // MUL
      fprintf(out, "MUL %d %d %d\n", A(code), B(code), C(code)); break;
    case 15: // DIV
      fprintf(out, "DIV %d %d %d\n", A(code), B(code), C(code)); break;
    case 16: // MOD
      fprintf(out, "MOD %d %d %d\n", A(code), B(code), C(code)); break;
    case 17: // POW
      fprintf(out, "POW %d %d %d\n", A(code), B(code), C(code)); break;
    case 18: // UNM
      fprintf(out, "UNM %d %d\n", A(code), B(code)); break;
    case 19: // NOT
      fprintf(out, "NOT %d %d\n", A(code), B(code)); break;
    case 20: // LEN
      fprintf(out, "LEN %d %d\n", A(code), B(code)); break;
    case 21: // CONCAT
      fprintf(out, "CONCAT %d %d %d\n", A(code), B(code), C(code)); break;
    case 22: // JMP
      fprintf(out, "JMP %d\n", PAYLOAD(code)); break;
    case 23: // EQ
      fprintf(out, "EQ %d %d %d\n", A(code), B(code), C(code)); break;
    case 24: // LT
      fprintf(out, "LT %d %d %d\n", A(code), B(code), C(code)); break;
    case 25: // LE
      fprintf(out, "LE %d %d %d\n", A(code), B(code), C(code)); break;
    case 26: // TEST
      fprintf(out, "TEST %d %d\n", A(code), B(code)); break;
    case 27: // TESTSET
      fprintf(out, "TESTSET %d %d %d\n", A(code), B(code), C(code)); break;
    case 28: // CALL
      fprintf(out, "CALL %d %d %d\n", A(code), B(code), C(code)); break;
    case 29: // TAILCALL
      fprintf(out, "TAILCALL %d %d %d\n", A(code), B(code), C(code)); break;
    case 30: // RETURN
      fprintf(out, "RETURN %d %d\n", A(code), B(code)); break;
    case 31: // FORLOOP
      fprintf(out, "FORPREP %d %d\n", A(code), PAYLOAD(code)); break;
    case 32: // FORPREP
      fprintf(out, "FORLOOP %d %d\n", A(code), PAYLOAD(code)); break;
    case 33: // TFORLOOP
      fprintf(out, "TFORLOOP %d %d\n", A(code), C(code)); break;
    case 34: // SETLIST
      fprintf(out, "SETLIST %d %d %d\n", A(code), B(code), C(code)); break;
    case 35: // CLOSE
      fprintf(out, "CLOSE %d\n", A(code)); break;
    case 36: // CLOSURE
      fprintf(out, "CLOSURE %d %d\n", A(code), PAYLOAD(code)); break;
    case 37: // VARARG
      fprintf(out, "VARARG %d %d\n", A(code), B(code)); break;

    default:
      printf("Bad opcode: 0x%08x", code);
      exit(1);
  }
}
