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

void luac_parse_file(lfunc_t *func, char *filename);
void luac_parse_string(lfunc_t *func, char *code, size_t csz, char *origin);
int  luac_parse_bytecode(lfunc_t *func, int fd, char *filename);
void luac_free(lfunc_t *func);

#endif /* _PARSE_H_ */
