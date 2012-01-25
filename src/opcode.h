#ifndef _OPCODE_H
#define _OPCODE_H

#include <stdint.h>
#include <stdio.h>

#define OP_MOVE 0
#define OP_LOADK 1
#define OP_LOADBOOL 2
#define OP_LOADNIL 3
#define OP_GETUPVAL 4
#define OP_GETGLOBAL 5
#define OP_GETTABLE 6
#define OP_SETGLOBAL 7
#define OP_SETUPVAL 8
#define OP_SETTABLE 9
#define OP_NEWTABLE 10
#define OP_SELF 11
#define OP_ADD 12
#define OP_SUB 13
#define OP_MUL 14
#define OP_DIV 15
#define OP_MOD 16
#define OP_POW 17
#define OP_UNM 18
#define OP_NOT 19
#define OP_LEN 20
#define OP_CONCAT 21
#define OP_JMP 22
#define OP_EQ 23
#define OP_LT 24
#define OP_LE 25
#define OP_TEST 26
#define OP_TESTSET 27
#define OP_CALL 28
#define OP_TAILCALL 29
#define OP_RETURN 30
#define OP_FORLOOP 31
#define OP_FORPREP 32
#define OP_TFORLOOP 33
#define OP_SETLIST 34
#define OP_CLOSE 35
#define OP_CLOSURE 36
#define OP_VARARG 37

#define OP(opc)       ((opc) & 0x3f)
#define A(opc)        (((opc) >> 6) & 0xff)
#define B(opc)        (((opc) >> 23) & 0x1ff)
#define C(opc)        (((opc) >> 14) & 0x1ff)
#define PAYLOAD(opc)  (((opc) >> 14) & 0x3ffff)
#define UNBIAS(v)     ((v) - 131071)

void opcode_dump(FILE *out, uint32_t code);

#endif /* _OPCODE_H */
