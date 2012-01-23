#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include "parse.h"
#include "lstring.h"
#include "vm.h"

int main(int argc, char **argv) {
  assert(argc > 1);
  int fd = open(argv[1], O_RDONLY);
  assert(fd != -1);

  luac_file_t *f = luac_open(fd);
  printf("opened\n");
  luac_parse(f);
  printf("parsed\n");

  // TODO - do stuff with the file

  luac_close(f);
  printf("closed\n");
  return 0;

  /*assert(argc > 1);
  parse_file(argv[1]);
  return 0;*/
}
