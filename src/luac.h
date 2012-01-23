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
  lfunc_t   func;
} luac_file_t;

typedef struct luac_header {
  u32 signature;
  u8  version;
  u8  format;
  u8  endianness;
  u8  int_size;
  u8  size_t_size;
  u8  instr_size;
  u8  num_size;
  u8  int_flag;
} __attribute__((packed)) luac_header_t;

luac_file_t *luac_open(int fd);
void luac_close(luac_file_t *file);
void luac_parse(luac_file_t *file);
u8 *luac_parse_func(u8 *addr, lfunc_t *func);

#endif /* _LUAC_H_ */
