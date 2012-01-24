#include <assert.h>
#include <stdio.h>

#include "lstring.h"

#define BUF_SIZE 80000

char buf[BUF_SIZE];

int main() {
  lstr_init();

  // generate a bunch of random characters
  size_t i;
  u8 v = 1;
  for (i = 0; i < BUF_SIZE; i++)
    buf[i] = (char) v++;

  printf("--- GENERATED ---\n");

  // create a bunch of strings
  for (i = 0; i < BUF_SIZE / 2; i++) {
    // these should all be new strings so they should be new idicies
    size_t idx = lstr_add(buf, i, 0);
    assert(idx == i);
  }
  printf("--- STEP 2 ---\n");

  for (i = 0; i < BUF_SIZE / 2; i++) {
    size_t idx = lstr_add(buf + BUF_SIZE / 2, i+1, 0);
    assert(idx == i + BUF_SIZE / 2);
  }

  printf("--- STEP 3 ---\n");

  for (i = 0; i < BUF_SIZE / 2; i++) {
    // these should return the old indices
    size_t idx = lstr_add(buf, i, 0);
    if (idx != i) {
      printf("idx = %zu, i = %zu\n", idx, i);
    }
    assert(idx == i);
  }
  
  printf("--- PASSED! ---\n");

  return 0;
}
