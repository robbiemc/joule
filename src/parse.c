#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "luav.h"
#include "panic.h"
#include "parse.h"
#include "util.h"
#include "vm.h"

static void luac_free_func(lfunc_t *func);
static u8* luac_parse_func(u8* addr, lfunc_t *func, char *filename);

/**
 * @brief Parse a precompiled file
 *
 * @param file the struct to fill in information for
 * @param filename the path to the file to parse
 * @return the parsed description of the file.
 */
void luac_parse_compiled(luac_file_t *file, char *filename) {
  int fd = open(filename, O_RDONLY);
  // get the file size
  struct stat finfo;
  int err = fstat(fd, &finfo);
  xassert(err == 0);
  size_t size = (size_t) finfo.st_size;

  // mmap the file
  void *addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  xassert(addr != MAP_FAILED);

  luac_parse(file, addr, SRC_MMAP, filename);
  file->size = size;
  close(fd);
}

/**
 * @brief Reads the given FILE* into memory and parses it. The FILE should
 *        contain bytecode.
 *
 * @param file the struct to fill in information for
 * @param f the file to read
 * @param origin the origin of the code (filename, for example)
 */
static void luac_parse_stream(luac_file_t *file, int fd, char *origin) {
  size_t buf_size = 1024;
  size_t len = 0;
  char *buf = xmalloc(buf_size);
  while (1) {
    ssize_t tmp = read(fd, buf + len, buf_size - len);
    if (tmp == 0) break;
    if (errno == EINTR) continue;
    assert(tmp > 0);
    len += (size_t) tmp;
    buf_size *= 2;
    buf = xrealloc(buf, buf_size);
  }

  luac_parse(file, buf, SRC_MALLOC, origin);
}

/**
 * @brief Parses the given lua source
 *
 * @param file the struct to fill in information for
 * @param code the lua source code
 * @param csz the length of the source string
 * @param origin the origin of the code (filename, for example)
 */
void luac_parse_string(luac_file_t *file, char *code, size_t csz, char *origin) {
  int err = 0;

  // create the pipes
  int in_fds[2];
  int out_fds[2];
  err = pipe(in_fds);
  xassert(err >= 0);
  err = pipe(out_fds);
  xassert(err >= 0);

  // fork!
  int pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    // child
    close(in_fds[1]);
    close(out_fds[0]);
    dup2(in_fds[0], STDIN_FILENO);
    dup2(out_fds[1], STDOUT_FILENO);
    execl("/bin/sh", "sh", "-c", "luac -o - -", NULL);
    panic("exec is returning!");
  }

  // parent
  close(in_fds[0]);
  close(out_fds[1]);

  // Make sure we send all the data
  size_t sent = 0;
  while (sent < csz) {
    ssize_t tmp = write(in_fds[1], code + sent, csz - sent);
    if (tmp < 0 && errno == EINTR) continue;
    assert(tmp >= 0);
    sent += (size_t) tmp;
  }
  close(in_fds[1]);

  luac_parse_stream(file, out_fds[0], origin);
  close(out_fds[0]);
  xassert(wait(NULL) == pid);
}

/**
 * @brief Parse the lua file (source code) given
 *
 * @param file the struct to fill in information for
 * @param filename the filename to open
 */
void luac_parse_source(luac_file_t *file, char *filename) {
  char cmd_prefix[] = "luac -o - ";
  char *cmd = xmalloc(sizeof(cmd_prefix) + strlen(filename) + 1);
  strcpy(cmd, cmd_prefix);
  strcpy(cmd + sizeof(cmd_prefix) - 1, filename);

  FILE *f = popen(cmd, "r");
  assert(f != NULL);
  luac_parse_stream(file, fileno(f), filename);
  pclose(f);
  free(cmd);
}

/**
 * @brief Parse a luac file which is visible at the specified address
 *
 * @param file the struct to parse into
 * @param addr the address at which the luac file is visible
 * @param source an integer SRC_* specifying where the file came from
 * @param filename the name that should be attributed to the source
 * @return the parsed description of the file.
 */
void luac_parse(luac_file_t *file, void *addr, int source, char *filename) {
  file->addr = addr;
  file->source = source;

  // read and validate the file header
  luac_header_t *header = addr;
  xassert(header->signature == 0x61754C1B);
  xassert(header->version == 0x51);
  xassert(header->format == 0);
  xassert(header->endianness == 1); // 0 = big endian, 1 = little endian
  xassert(header->int_size == sizeof(int));
  xassert(header->size_t_size == sizeof(size_t));
  xassert(header->instr_size == 4);
  xassert(header->num_size == sizeof(double));
  xassert(header->int_flag == 0); // 0 = doubles, 1 = integers

  // parse the main function
  luac_parse_func((u8*)(header + 1), &file->func, filename);
}

/**
 * @brief Close a file, freeing any resources possibly allocated with it
 *
 * @param file the file to close which was previously filled in by parsing
 */
void luac_close(luac_file_t *file) {
  // free everything then unmap the file if necessary
  luac_free_func(&file->func);
  switch (file->source) {
    case SRC_MMAP:
      munmap(file->addr, file->size);
      break;
    case SRC_MALLOC:
      free(file->addr);
      break;
  }
}

static void luac_free_func(lfunc_t *func) {
  free(func->consts);
  u32 i;
  for (i = 0; i < func->num_funcs; i++) {
    luac_free_func(&func->funcs[i]);
  }
  if (func->num_funcs > 0) {
    free(func->funcs);
  }
}

static u8* luac_parse_func(u8 *addr, lfunc_t *func, char *filename) {
  u32 size, i;
  size_t length = pread8(&addr);
  func->name = lstr_add((char*) addr, length - !!length, FALSE);
  func->file = filename;

  // read the function header
  addr += length;
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
        c[i] = LUAV_NIL;
        break;
      case LUAC_TBOOLEAN:
        c[i] = lv_bool(pread1(&addr));
        break;
      case LUAC_TNUMBER:
        c[i] = lv_number(lv_cvt(pread8(&addr)));
        break;
      case LUAC_TSTRING:
        length = pread8(&addr);
        c[i] = lv_string(lstr_add((char*) addr, length - !!length, FALSE));
        addr += length;
        break;
      default:
        panic("Corrupt precompiled file");
    }
  }

  func->num_funcs = pread4(&addr);
  if (func->num_funcs == 0) {
    func->funcs = NULL;
  } else {
    func->funcs = xcalloc(func->num_funcs, sizeof(lfunc_t));
    for (i = 0; i < func->num_funcs; i++)
      addr = luac_parse_func(addr, &(func->funcs[i]), filename);
  }

  func->dbg_linecount = pread4(&addr);
  func->dbg_lines = (u32*) addr;
  addr += func->dbg_linecount * sizeof(u32);

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
