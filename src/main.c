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
    luac_parse_source(&file, argv[i]);
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
