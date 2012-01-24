#ifndef _PARSE_H_
#define _PARSE_H_

#include <stdint.h>

#include "config.h"
#include "lstring.h"
#include "vm.h"

#define LUAV_TNIL      0
#define LUAV_TBOOLEAN  1
#define LUAV_TNUMBER   3
#define LUAV_TSTRING   4

#define SKIP_STRING(ptr) ((u8*)(ptr) + ((lstring_t*)(ptr))->length + sizeof(size_t))

typedef struct luac_file {
  int       mmapped;
  void*     addr;
  size_t    size;
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
} PACKED luac_header_t;

void luac_parse_fd(luac_file_t *file, int fd);
void luac_parse(luac_file_t *file, void *addr);
void luac_close(luac_file_t *file);

#endif /* _PARSE_H_ */
