#include <assert.h>
#include "parse.h"

int main(int argc, char **argv) {
  assert(argc > 1);
  parse_file(argv[1]);
  return 0;
}
