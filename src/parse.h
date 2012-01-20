#ifndef _PARSE_H
#define _PARSE_H

#include <stdint.h>

#define LUA_TNIL      0
#define LUA_TBOOLEAN  1
#define LUA_TNUMBER   3
#define LUA_TSTRING   4

typedef struct lua_header {
  uint32_t  signature;
  uint8_t   version;
  uint8_t   format;
  uint8_t   endianness;
  uint8_t   int_size;
  uint8_t   size_t_size;
  uint8_t   instr_size;
  uint8_t   num_size;
  uint8_t   int_flag;
} __attribute__((packed)) lua_header_t;

typedef struct lua_function {
  int     start_line;
  int     end_line;
  uint8_t upvalues;
  uint8_t parameters;
  uint8_t is_vararg;
  uint8_t max_stack;
} __attribute__((packed)) lua_function_t;

void parse_file(char *path);

#endif /* _PARSE_H */
