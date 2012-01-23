#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "luac.h"
#include "util.h"


luac_file_t *luac_open(int fd) {
  // get the file size
  struct stat finfo;
  int err = fstat(fd, &finfo);
  if (err != 0) return NULL;
  off_t size = finfo.st_size;
  // mmap the file and fill in the struct
  void *addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) return NULL;
  // everything succeeded - allocate the luac_file struct
  luac_file_t *file = (luac_file_t*) amalloc(sizeof(luac_file_t));
  file->fd = fd;
  file->addr = addr;
  file->size = size;
  file->func = NULL;
  return file;
}

void luac_close(luac_file_t *file) {
  // unmap the file then free the struct
  munmap(file->addr, file->size);
  free(file);
}

void luac_parse(luac_file_t *file) {
  // read and validate the file header
  luac_header_t header;
  memcpy(&header, file->addr, sizeof(luac_header_t));
  assert(header.signature == 0x61754C1B);
  assert(header.version == 0x51);
  assert(header.format == 0);
  assert(header.endianness == 1);
  assert(header.int_size == 4);
  assert(header.size_t_size == 8);
  assert(header.instr_size == 4);
  assert(header.num_size == 8);
  assert(header.int_flag == 0);

  // parse the main function
  void *func_start = (void*)((u8*)file->addr + sizeof(luac_header_t));
  file->func = luac_parse_func(func_start);
}

lfunc_t *luac_parse_func(void *fdata) {
  lfunc_t *func = (lfunc_t*) amalloc(sizeof(lfunc_t));
  func->name = (lstring_t*) fdata; // name is the first thing in the header

  uint8_t *addr = (uint8_t*) fdata;

  // read the function header
  addr = SKIP_STRING(addr);
  size_t hdr_size = 12; // FIXME - magic number
  memcpy(&(func->start_line), addr, hdr_size);
  addr += hdr_size;

  func->num_instrs = pread4(&addr);
  func->instrs = (u32*) addr;
  addr += func->num_instrs * sizeof(u32);

  func->num_consts = pread4(&addr);
  func->consts = (lvalue*) calloc(func->num_consts, sizeof(lvalue));
  // TODO iterate over the constant list then do all the other lists

  return func;
}
