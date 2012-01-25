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

static void luac_free_func(lfunc_t *func);
static u8* luac_parse_func(u8* addr, lfunc_t *func);

/**
 * @brief Parse a luac file given by the file descriptor
 *
 * @param file the struct to fill in information for
 * @param fd the file descriptor which when read, will return the luac file.
 * @return the parsed description of the file.
 */
void luac_parse_fd(luac_file_t *file, int fd) {
  // get the file size
  struct stat finfo;
  int err = fstat(fd, &finfo);
  assert(err == 0);
  size_t size = (size_t) finfo.st_size;

  // mmap the file
  void *addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(addr != MAP_FAILED);

  luac_parse(file, addr);
  file->mmapped = 1;
  file->size = size;
}

/**
 * @brief Parse a luac file which is visible at the specified address
 *
 * @param file the struct to parse into
 * @param addr the address at which the luac file is visible
 * @return the parsed description of the file.
 */
void luac_parse(luac_file_t *file, void *addr) {
  file->addr = addr;
  file->mmapped = 0;

  // read and validate the file header
  luac_header_t *header = addr;
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
  luac_parse_func((u8*)(header + 1), &file->func);
}

/**
 * @brief Close a file, freeing any resources possibly allocated with it
 *
 * @param file the file to close which was previously filled in by parsing
 */
void luac_close(luac_file_t *file) {
  // free everything then unmap the file if necessary
  luac_free_func(&file->func);
  if (file->mmapped) {
    munmap(file->addr, file->size);
  }
}

static void luac_free_func(lfunc_t *func) {
  free(func->consts);
  u32 i;
  for (i = 0; i < func->num_funcs; i++) {
    luac_free_func(&func->funcs[i]);
  }
  free(func->funcs);
}

static u8* luac_parse_func(u8 *addr, lfunc_t *func) {
  func->name = lstr_add((char*)addr + sizeof(size_t), *((size_t*)addr), FALSE);

  u32 size, i;
  size_t length;

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
      case LUAC_TNIL:
        *c = LUAV_NIL;
        break;
      case LUAC_TBOOLEAN:
        *c = lv_bool(pread1(&addr));
        break;
      case LUAC_TNUMBER:
        *c = lv_number(lv_cvt(pread8(&addr)));
        break;
      case LUAC_TSTRING:
        length = pread8(&addr);
        *c = lv_string(lstr_add((char*)addr, length, FALSE));
        addr += length;
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
