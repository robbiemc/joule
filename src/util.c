#include <assert.h>
#include <stdlib.h>

#include "util.h"

u8 pread1(u8 **p) {
  u8 b = **p;
  *p += sizeof(u8);
  return b;
}

u32 pread4(u8 **p) {
  u32 b = *(u32*)*p;
  *p += sizeof(u32);
  return b;
}

u64 pread8(u8 **p) {
  u64 b = *(u64*)*p;
  *p += sizeof(u64);
  return b;
}

void *xmalloc(size_t s) {
  void *m = malloc(s);
  assert(m != NULL);
  return m;
}

void *xcalloc(size_t n, size_t s) {
  void *m = calloc(n, s);
  assert(m != NULL);
  return m;
}
