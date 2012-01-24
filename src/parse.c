#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "config.h"
#include "luav.h"
#include "parse.h"
#include "util.h"
#include "vm.h"

luac_file_t *luac_open(int fd) {
  // get the file size
  struct stat finfo;
  int err = fstat(fd, &finfo);
  if (err != 0) return NULL;
  size_t size = (size_t) finfo.st_size;
  // mmap the file and fill in the struct
  void *addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) return NULL;
  // everything succeeded - allocate the luac_file struct
  luac_file_t *file = (luac_file_t*) xmalloc(sizeof(luac_file_t));
  file->fd = fd;
  file->addr = addr;
  file->size = size;
  memset(&(file->func), 0, sizeof(lfunc_t));
  return file;
}

void luac_free_func(lfunc_t *func) {
  free(func->consts);
  u32 i;
  for (i = 0; i < func->num_funcs; i++)
    luac_free_func(&(func->funcs[i]));
  free(func->funcs);
}
void luac_close(luac_file_t *file) {
  // free everything then unmap the file
  luac_free_func(&(file->func));
  munmap(file->addr, file->size);
  free(file);
}

void luac_parse(luac_file_t *file) {
  // read and validate the file header
  luac_header_t *header = file->addr;
  assert(header->signature == 0x61754C1B);
  assert(header->version == 0x51);
  assert(header->format == 0);
  assert(header->endianness == 1); // 0 = big endian, 1 = little endian
  assert(header->int_size == sizeof(int));
  assert(header->size_t_size == sizeof(size_t));
  assert(header->instr_size == 4);
  assert(header->num_size == sizeof(double));
  assert(header->int_flag == 0); // 0 = doubles, 1 = integers

  // parse the main function
  luac_parse_func((u8*)(header + 1), &(file->func));
}

u8 *luac_parse_func(u8 *addr, lfunc_t *func) {
  func->name = (lstring_t*) addr; // name is the first thing in the header

  u32 size, i;

  // read the function header
  addr = SKIP_STRING(addr);
  size_t hdr_size = 12; // FIXME - magic number
  memcpy(&(func->start_line), addr, hdr_size);
  addr += hdr_size;

  func->num_instrs = pread4(&addr);
  func->instrs = (u32*) addr;
  addr += func->num_instrs * sizeof(u32);

  func->num_consts = pread4(&addr);
  func->consts = (luav*) xcalloc(func->num_consts, sizeof(luav));
  luav *c = func->consts;
  for (i = 0; i < func->num_consts; i++) {
    switch (pread1(&addr)) {
      case LUA_TNIL:
        *c = lv_nil();
        break;
      case LUA_TBOOLEAN:
        *c = lv_bool(pread1(&addr));
        break;
      case LUA_TNUMBER:
        *c = lv_number(pread8(&addr));
        break;
      case LUA_TSTRING:
        *c = lv_string((lstring_t*) addr);
        addr = SKIP_STRING(addr);
        break;
      default:
        assert(0); // TODO - figure out how we're actually going to handle errors
    }
    c++;
  }

  func->num_funcs = pread4(&addr);
  func->funcs = xcalloc(func->num_funcs, sizeof(lfunc_t));
  for (i = 0; i < func->num_funcs; i++)
    addr = luac_parse_func(addr, &(func->funcs[i]));

  func->dbg_lines = addr;
  size = pread4(&addr);
  addr += size * sizeof(u32);

  func->dbg_locals = addr;
  size = pread4(&addr);
  for (i = 0; i < size; i++) {
    addr = SKIP_STRING(addr);
    addr += 2 * sizeof(u32);
  }

  func->dbg_upvalues = addr;
  size = pread4(&addr);
  for (i = 0; i < size; i++)
    addr = SKIP_STRING(addr);

  return addr;
}
