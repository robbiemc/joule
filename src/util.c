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

uint64_t read8(FILE *f) {
  uint64_t quad;
  size_t count = fread(&quad, sizeof(quad), 1, f);
  assert(count == 1);
  return quad;
}

uint8_t pread1(uint8_t **p) {
  uint8_t b = **p;
  *p += sizeof(uint8_t);
  return b;
}

uint32_t pread4(uint8_t **p) {
  uint32_t b = *(uint32_t*)*p;
  *p += sizeof(uint32_t);
  return b;
}

uint64_t pread8(uint8_t **p) {
  uint64_t b = *(uint64_t*)*p;
  *p += sizeof(uint64_t);
  return b;
}

void *amalloc(size_t s) {
  void *m = malloc(s);
  assert(m != NULL);
  return m;
}
