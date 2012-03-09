#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "gc.h"
#include "luav.h"
#include "panic.h"
#include "parse.h"
#include "util.h"
#include "vm.h"

static int luac_parse_func(lfunc_t *func, int fd, char *filename, u8 st_size);

/**
 * @brief Parses the given lua source
 *
 * @param func the struct to fill in information for
 * @param code the lua source code
 * @param csz the length of the source string
 * @param origin the origin of the code (filename, for example)
 */
int luac_parse_string(lfunc_t *func, char *code, size_t csz, char *origin) {
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
    execl("/bin/sh", "sh", "-c", "luac -o - - 2>/dev/null", NULL);
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

  int res = luac_parse_bytecode(func, out_fds[0], origin);
  close(out_fds[0]);
  return res;
}

/**
 * @brief Parse the lua file (source code) given
 *
 * @param func the struct to fill in information for
 * @param filename the filename to open
 */
int luac_parse_file(lfunc_t *func, char *filename) {
  char cmd_prefix[] = "luac -o - -- ";
  char *cmd = xmalloc(sizeof(cmd_prefix) + strlen(filename) + 1);
  strcpy(cmd, cmd_prefix);
  strcpy(cmd + sizeof(cmd_prefix) - 1, filename);

  FILE *f = popen(cmd, "r");
  assert(f != NULL);
  int res = luac_parse_bytecode(func, fileno(f), filename);
  pclose(f);
  free(cmd);
  return res;
}

/**
 * @brief Parse a luac file which is visible at the specified address
 *
 * @param func the struct to parse into
 * @param fd the file descriptor to read from
 * @param filename the name that should be attributed to the source
 * @return return code
 */
int luac_parse_bytecode(lfunc_t *func, int fd, char *filename) {
  // read and validate the file header
  luac_header_t header;
  xread(fd, &header, sizeof(header));

  if (header.signature   != 0x61754C1B)     return -1;
  if (header.version     != 0x51)           return -1;
  if (header.format      != 0)              return -1;
  if (header.endianness  != 1)              return -1; // 1 = little endian
  if (header.int_size    != sizeof(int))    return -1;
  if (header.instr_size  != 4)              return -1;
  if (header.num_size    != sizeof(double)) return -1;
  if (header.int_flag    != 0)              return -1; // 0 = doubles

  // parse the main function
  return luac_parse_func(func, fd, filename, header.size_t_size);

fderr:
  return -1;
}

static int luac_skip(int fd, size_t len) {
  // apparently lseek doesn't work on all streams, so read the
  // data into a buffer in chunks
  static char buf[512];
  while (len > 0) {
    ssize_t rd = read(fd, buf, MIN(sizeof(buf), len));
    if (rd == -1) return -1;
    len -= (size_t) rd;
  }
  return 0;
}

static lstring_t *luac_read_string_(int fd, u8 st_size) {
  size_t len = (size_t) (st_size == 8 ? xread8(fd) : xread4(fd));
  if (len <= 1) {
    if (len == 1) xassert(xread1(fd) == 0);
    return lstr_empty();
  }
  lstring_t *str = lstr_alloc(len - 1);
  xread(fd, str->data, len);
  return lstr_add(str);
fderr:
  return NULL;
}

#define luac_skip_string(fd, s) \
  luac_skip(fd, (size_t) ((s) == 8 ? xread8(fd) : xread4(fd)));
#define luac_read_string(fd, s) ({                   \
          lstring_t *str = luac_read_string_(fd, s); \
          if (str == NULL) goto fderr;               \
          str;                                       \
        })

static int luac_parse_func(lfunc_t *func, int fd, char *filename, u8 st_size) {
  size_t size, i;
  // file / function name
  func->file = filename;
  func->name = luac_read_string(fd, st_size);

  // function metadata
  func->start_line     = xread4(fd);
  func->end_line       = xread4(fd);
  func->num_upvalues   = xread1(fd);
  func->num_parameters = xread1(fd);
  func->is_vararg      = xread1(fd);
  func->max_stack      = xread1(fd);

  // instructions
  func->num_instrs = xread4(fd);
  size = func->num_instrs * sizeof(u32);
  func->instrs = gc_alloc(size, LANY);
  xread(fd, func->instrs, size);

  // constants :(
  func->num_consts = xread4(fd);
  func->consts = gc_alloc(func->num_consts * sizeof(luav), LANY);
  luav *c = func->consts;
  for (i = 0; i < func->num_consts; i++) {
    switch (xread1(fd)) {
      case LUAC_TNIL:
        c[i] = LUAV_NIL;
        break;
      case LUAC_TBOOLEAN:
        c[i] = lv_bool(xread1(fd));
        break;
      case LUAC_TNUMBER:
        c[i] = lv_number(lv_cvt(xread8(fd)));
        break;
      case LUAC_TSTRING:
        c[i] = lv_string(luac_read_string(fd, st_size));
        break;
      default:
        goto fderr;
    }
  }

  // nested functions
  func->num_funcs = xread4(fd);
  if (func->num_funcs == 0) {
    func->funcs = NULL;
  } else {
    func->funcs = gc_alloc(func->num_funcs * sizeof(lfunc_t*), LANY);
    for (i = 0; i < func->num_funcs; i++) {
      lfunc_t *f = gc_alloc(sizeof(lfunc_t), LFUNC);
      int ret = luac_parse_func(f, fd, filename, st_size);
      func->funcs[i] = f;
      if (ret < 0) goto fderr;
    }
  }

  // source lines
  func->num_lines = xread4(fd);

  size = func->num_lines * sizeof(u32);
  func->lines = gc_alloc(size, LANY);
  xread(fd, func->lines, size);

  // skip locals debug data
  size = xread4(fd);
  for (i = 0; i < size; i++) {
    luac_skip_string(fd, st_size);
    luac_skip(fd, 2 * sizeof(u32));
  }
  // skip upvalues debug data
  size = xread4(fd);
  for (i = 0; i < size; i++) {
    luac_skip_string(fd, st_size);
  }

  return 0;
fderr:
  return -1;
}
