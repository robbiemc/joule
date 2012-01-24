#ifndef _PARSE_H_
#define _PARSE_H_

#include <stdint.h>
#include "lstring.h"
#include "vm.h"

#define LUAV_TNIL      0
#define LUAV_TBOOLEAN  1
#define LUAV_TNUMBER   3
#define LUAV_TSTRING   4

#define SKIP_STRING(ptr) ((u8*)(ptr) + ((lstring_t*)(ptr))->length + sizeof(size_t))

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

#endif /* _PARSE_H_ */
