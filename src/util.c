#include <assert.h>

#include "util.h"

uint8_t read1(FILE *f) {
  uint8_t byte;
  size_t count = fread(&byte, sizeof(byte), 1, f);
  assert(count == 1);
  return byte;
}

uint32_t read4(FILE *f) {
  uint32_t word;
  size_t count = fread(&word, sizeof(word), 1, f);
  assert(count == 1);
  return word;
}
