#ifndef _LUAC_H_
#define _LUAC_H_

#include <stdint.h>
#include "lstring.h"
#include "vm.h"

#define LUA_TNIL      0
#define LUA_TBOOLEAN  1
#define LUA_TNUMBER   3
#define LUA_TSTRING   4

typedef struct luac_file {
  int       fd;
  void*     addr;
  off_t     size;
  lfunc_t   *func;
} luac_file_t;

typedef struct luac_header {
  uint32_t  signature;
  uint8_t   version;
  uint8_t   format;
  uint8_t   endianness;
  uint8_t   int_size;
  uint8_t   size_t_size;
  uint8_t   instr_size;
  uint8_t   num_size;
  uint8_t   int_flag;
} __attribute__((packed)) luac_header_t;

typedef struct luac_func {
  int     start_line;
  int     end_line;
  uint8_t upvalues;
  uint8_t parameters;
  uint8_t is_vararg;
  uint8_t max_stack;
} __attribute__((packed)) luac_func_t;


luac_file_t *luac_open(int fd);

void luac_close(luac_file_t *file);

void luac_parse(luac_file_t *file);

lfunc_t *luac_parse_func(void *addr);


#endif /* _LUAC_H_ */
