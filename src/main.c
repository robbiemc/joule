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

static lhash_t lua_arg;

static void register_argv(int argc, char **argv) {
  int i;
  lhash_init(&lua_arg);
  lhash_set(&lua_globals, LSTR("arg"), lv_table(&lua_arg));

  for (i = 0; i < argc; i++) {
    lstr_idx idx = lstr_add(argv[i], strlen(argv[i]) + 1, FALSE);
    lhash_set(&lua_arg, lv_number(i - 1), lv_string(idx));
  }
}

int main(int argc, char **argv) {
  luac_file_t file;
  assert(argc > 1);
  int fd = open(argv[1], O_RDONLY);
  assert(fd != -1);
  luac_parse_fd(&file, fd);
  close(fd);

  int i;
  for (i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0) {
      dbg_dump_function(stdout, &(file.func));
      break;
    }
  }

  register_argv(argc, argv);
  vm_run(&file.func);

  luac_close(&file);
  return 0;
}
