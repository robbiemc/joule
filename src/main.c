#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "lhash.h"
#include "lstring.h"
#include "parse.h"
#include "vm.h"

#define SET(i,str) (strcmp(argv[(i)], (str)) == 0)

static lhash_t lua_arg;

typedef struct lflags {
  char  dump;
  char  compiled;
} lflags_t;

static int parse_args(lflags_t *flags, int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    if (SET(i, "-d"))
      flags->dump = TRUE;
    else if (SET(i, "-c"))
      flags->compiled = TRUE;
    else
      break;
  }
  return i;
}

static void *parse_file(char *filename) {
  char cmd_prefix[] = "luac -o /dev/stdout ";
  char *cmd = xmalloc(sizeof(cmd_prefix) + strlen(filename) + 1);
  strcpy(cmd, cmd_prefix);
  strcpy(cmd + sizeof(cmd_prefix) - 1, filename);
  FILE *f = popen(cmd, "r");
  free(cmd);

  size_t buf_size = 1024;
  size_t len = 0;
  char *buf = NULL;
  while (!feof(f) && !ferror(f)) {
    buf_size *= 2;
    buf = xrealloc(buf, buf_size);
    len += fread(&buf[len], 1, buf_size - len, f);
  }
  assert(ferror(f) == 0);
  pclose(f);
  return buf;
}

static void register_argv(int bias, int argc, char **argv) {
  lhash_init(&lua_arg);
  lhash_set(&lua_globals, LSTR("arg"), lv_table(&lua_arg));

  int i;
  for (i = 0; i < argc; i++) {
    lstr_idx idx = lstr_add(argv[i], strlen(argv[i]) + 1, FALSE);
    lhash_set(&lua_arg, lv_number(i - bias), lv_string(idx));
  }
}

int main(int argc, char **argv) {
  luac_file_t file;
  lflags_t flags;

  memset(&flags, 0, sizeof(lflags_t));
  int i = parse_args(&flags, argc, argv);
  if (i >= argc) goto usage;

  if (flags.compiled) {
    int fd = open(argv[i], O_RDONLY);
    assert(fd != -1);
    luac_parse_fd(&file, fd);
    close(fd);
  } else {
    void *bytecode = parse_file(argv[i]);
    luac_parse(&file, bytecode, SRC_MALLOC);
  }

  if (flags.dump)
    dbg_dump_function(stdout, &file.func);

  register_argv(i, argc, argv);
  vm_run(&file.func);
  lhash_free(&lua_arg);

  luac_close(&file);
  return 0;

usage:
  printf("Usage: %s [-c] [-d] <file>\n", argv[0]);
  return 1;
}
