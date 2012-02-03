#include <stdlib.h>
#include <unistd.h>

#include "panic.h"
#include "util.h"

int fdread(int fd, void *buf, size_t len) {
  while (len > 0) {
    ssize_t rd = read(fd, buf, len);
    if (rd == -1) return -1;
    len -= (size_t) rd;
    buf += (size_t) rd;
  }
  return 0;
}

void* xmalloc(size_t s) {
  void *m = malloc(s);
  xassert(m != NULL);
  return m;
}

void* xcalloc(size_t n, size_t s) {
  void *m = calloc(n, s);
  xassert(m != NULL);
  return m;
}

void *xrealloc(void *p, size_t s) {
  void *m = realloc(p, s);
  xassert(m != NULL);
  return m;
}
