#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "error.h"
#include "lhash.h"
#include "lstring.h"
#include "panic.h"
#include "parse.h"
#include "vm.h"


#define SET(i,str) (strcmp(argv[(i)], (str)) == 0)

static lhash_t lua_arg;

typedef struct lflags {
  char  dump;
  char  compiled;
  char  string;
} lflags_t;

static int parse_args(lflags_t *flags, int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    if (SET(i, "-d"))
      flags->dump = TRUE;
    else if (SET(i, "-c"))
      flags->compiled = TRUE;
    else if (SET(i, "-e"))
      flags->string = TRUE;
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
  lfunc_t func;
  lflags_t flags;

  memset(&flags, 0, sizeof(lflags_t));
  int i = parse_args(&flags, argc, argv);
  if (i >= argc) goto usage;

  lua_program = argv[0];
  if (flags.compiled) {
    int fd = open(argv[i], O_RDONLY);
    xassert(fd != -1);
    luac_parse_bytecode(&func, fd, argv[i]);
    close(fd);
  } else if (flags.string) {
    luac_parse_string(&func, argv[i], strlen(argv[i]), "(command line)");
  } else {
    luac_parse_file(&func, argv[i]);
  }

  if (flags.dump) {
    dbg_dump_function(stdout, &func);
    return 0;
  }

  register_argv(i, argc, argv);
#ifdef HASH_PROFILE
  lhash_start_profile();
#endif
  vm_run(&func);
#ifdef HASH_PROFILE
  lhash_show_profile();
#endif
  lhash_free(&lua_arg);

  luac_free(&func);
  return 0;

usage:
  printf("Usage: %s [-c] [-d] [-e] <file>\n", argv[0]);
  return 1;
}
