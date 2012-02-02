#ifndef _PARSE_H_
#define _PARSE_H_

#include <stdint.h>

#include "config.h"
#include "lstring.h"
#include "vm.h"

#define LUAC_TNIL       0
#define LUAC_TBOOLEAN   1
#define LUAC_TNUMBER    3
#define LUAC_TSTRING    4

#define SKIP_STRING(ptr) ((u8*)(ptr) + *((size_t*)(ptr)) + sizeof(size_t))

typedef struct luac_file {
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

void luac_parse_stream(luac_file_t *file, int fd, char *origin);
void luac_parse_source(luac_file_t *file, char *filename);
void luac_parse_string(luac_file_t *file, char *code, size_t csz, char *origin);
void luac_parse(luac_file_t *file, void *addr, char *filename);
void luac_close(luac_file_t *file);

#endif /* _PARSE_H_ */
