#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

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

  luac_close(&file);
  return 0;

  /*assert(argc > 1);
  parse_file(argv[1]);
  return 0;*/
}
