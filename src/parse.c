#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "opcode.h"
#include "parse.h"
#include "util.h"

#define BUFFER_SIZE 128

#define xfread(f, ptr) {                                 \
    size_t count = fread(ptr, sizeof(*ptr), 1, f);       \
    assert(count == 1);                                  \
  }

static void read_string(FILE *f);
static void read_function(FILE *f);
static void read_constant(FILE *f);

void parse_file(char *path) {
  FILE *f = fopen(path, "r");
  assert(f);

  // read the header
  lua_header_t header;
  xfread(f, &header);

  assert(header.size_t_size == sizeof(size_t));
  assert(header.int_size == sizeof(int));
  assert(header.instr_size == 4);

  printf("File Header\n");
  printf("  signature:  0x%08x\n", header.signature);
  printf("  version:    0x%02x\n", header.version);
  printf("  format:     0x%02x\n", header.format);
  printf("  endianness: 0x%02x\n", header.endianness);
  printf("  int_size:   0x%02x\n", header.int_size);
  printf("  |size_t|:   0x%02x\n", header.size_t_size);
  printf("  instr_size: 0x%02x\n", header.instr_size);
  printf("  num_size:   0x%02x\n", header.num_size);
  printf("  int_flag:   0x%02x\n", header.int_flag);

  read_function(f);

  long loc = ftell(f);
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  printf("At position %zu of %zu\n", loc, size);
  assert(loc == size);
}

static void read_function(FILE *f) {
  int i;
  // header
  printf("\nFunction Header: ");
  read_string(f);
  lua_function_t fun;
  xfread(f, &fun);
  printf("  start:      %d\n", fun.start_line);
  printf("  end:        %d\n", fun.end_line);
  printf("  upvalues:   %d\n", fun.upvalues);
  printf("  parameters: %d\n", fun.parameters);
  printf("  is_vararg:  %02x\n", fun.is_vararg);
  printf("  max_stack:  %d\n", fun.max_stack);

  // instructions
  printf("\nInstructions:\n");
  int codesize = read4(f);
  for (i = 0; i < codesize; i++) {
    uint32_t opcode = read4(f);
    printf("%2d: ", i);
    opcode_dump(stdout, opcode);
  }

  // constants
  printf("\nConstants:\n");
  int constants = read4(f);
  for (i = 0; i < constants; i++) {
    printf("%2d: ", i);
    read_constant(f);
  }

  printf("\n");

  // functions
  printf("\nNested Functions:\n");
  int functions = read4(f);
  for (i = 0; i < functions; i++) {
    printf("function %d:\n", i);
    read_function(f);
  }

  // source line positions
  printf("\nSource Line Positions:\n");
  int lineinfosize = read4(f);
  for (i = 0; i < lineinfosize; i++) {
    int line = read4(f);
    printf("%d at line %d\n", i, line);
  }

  // local variables
  printf("\nLocal Variables:\n");
  int localsize = read4(f);
  for (i = 0; i < localsize; i++) {
    printf("%2d: ", i);
    read_string(f);
    int start = read4(f),
        end   = read4(f);
    printf("    Scope: %d - %d\n", start, end);
  }

  // upvalues
  printf("\nUpvalues:\n");
  int upvalues = read4(f);
  for (i = 0; i < upvalues; i++) {
    printf("%2d: ", i);
    read_string(f);
  }
}

static void read_constant(FILE *f) {
  uint8_t byte;
  uint8_t type = read1(f);
  double constant;
  switch (type) {
    case LUA_TNIL:
      printf("nil\n");
      break;

    case LUA_TBOOLEAN:
      byte = read4(f);
      printf("%s\n", byte ? "true" : "false");
      break;

    case LUA_TNUMBER:
      xfread(f, &constant);
      printf("%f\n", constant);
      break;

    case LUA_TSTRING:
      read_string(f);
      break;

    default:
      printf("Bad constant type!\n");
      exit(1);
  }
}

static void read_string(FILE *f) {
  char buffer[BUFFER_SIZE];
  size_t bytes = read8(f);
  if (bytes == 0) {
    printf("<empty string>\n");
    return;
  }
  assert(bytes <= BUFFER_SIZE);
  uint32_t count = fread(buffer, 1, bytes, f);
  assert(count == bytes);
  printf("\"%s\"\n", buffer);
}
