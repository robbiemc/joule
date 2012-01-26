#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "debug.h"
#include "lstring.h"
#include "parse.h"
#include "vm.h"

int main(int argc, char **argv) {
  luac_file_t file;
  assert(argc > 1);
  int fd = open(argv[1], O_RDONLY);
  assert(fd != -1);
  luac_parse_fd(&file, fd);
  close(fd);

  // TODO - do stuff with the file
  dbg_dump_function(stdout, &(file.func));

  vm_run(&file.func);

  luac_close(&file);
  return 0;
}
