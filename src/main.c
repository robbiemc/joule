#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "error.h"
#include "flags.h"
#include "lhash.h"
#include "lstring.h"
#include "panic.h"
#include "parse.h"
#include "vm.h"

#define SET(i,str) (strcmp(argv[(i)], (str)) == 0)

static lhash_t lua_arg;

lflags_t flags;

static int parse_args(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    if (SET(i, "-d"))
      flags.dump = TRUE;
    else if (SET(i, "-c"))
      flags.compiled = TRUE;
    else if (SET(i, "-e"))
      flags.string = TRUE;
    else if (SET(i, "-p"))
      flags.print = TRUE;
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
    lstr_idx idx = lstr_add(argv[i], strlen(argv[i]), FALSE);
    lhash_set(&lua_arg, lv_number(i - bias), lv_string(idx));
  }
}

int main(int argc, char **argv) {
  int ret;
  lfunc_t func;

  memset(&flags, 0, sizeof(lflags_t));
  int i = parse_args(argc, argv);
  if (i >= argc) goto usage;

  lua_program = argv[0];
  if (flags.compiled) {
    int fd = open(argv[i], O_RDONLY);
    xassert(fd != -1);
    ret = luac_parse_bytecode(&func, fd, argv[i]);
    close(fd);
  } else if (flags.string) {
    ret = luac_parse_string(&func, argv[i], strlen(argv[i]), "(command line)");
  } else {
    ret = luac_parse_file(&func, argv[i]);
  }

  if (ret < 0) {
    printf("bad file/string provided\n");
    return 1;
  }

  if (flags.dump) {
    dbg_dump_function(stdout, &func);
    return 0;
  }

  register_argv(i, argc, argv);
  vm_run(&func);
  lhash_free(&lua_arg);

  luac_free(&func);
  return 0;

usage:
  printf("Usage: %s [options] <file>\n", argv[0]);
  printf("  -c  Execute precompiled lua bytecode\n");
  printf("  -e  Execute the provided string of lua\n");
  printf("  -d  Dump the program's instructions\n");
  printf("  -p  Print each instruction before it's executed\n");
  return 1;
}
