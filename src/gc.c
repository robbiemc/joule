#include <string.h>
#include <sys/mman.h>

#include "config.h"
#include "luav.h"
#include "panic.h"
#include "gc.h"

#define HEAP1_ADDR ((void*) 0x0000600000000000)
#define HEAP2_ADDR ((void*) 0x0000700000000000)

#define ALIGN8(n) (((n) + 7) & (~(size_t)7))

static void *heap;
static void *heap_secondary;
static size_t heap_size;
static size_t heap_next;

static void gc_mmap(size_t amount, off_t offset);

INIT static void gc_init() {
  // allocate two heaps
  heap_size = INIT_HEAP_SIZE;
  heap_next = 0;
  heap = HEAP1_ADDR;
  heap_secondary = HEAP2_ADDR;
  gc_mmap(heap_size, 0);
}

DESTROY static void gc_destroy() {
  xassert(munmap(HEAP1_ADDR, heap_size) == 0);
  xassert(munmap(HEAP2_ADDR, heap_size) == 0);
}

static void gc_mmap(size_t amount, off_t offset) {
  void *addr1 = HEAP1_ADDR + offset;
  xassert(mmap(addr1, amount, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0) == addr1);
  void *addr2 = HEAP2_ADDR + offset;
  xassert(mmap(addr2, heap_size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0) == addr2);
}

void *gc_calloc(size_t nelt, size_t eltsize) {
  size_t size = nelt * eltsize;
  size = ALIGN8(size);

  void *addr = gc_alloc(size);
  memset(addr, 0, size);
  return addr;
}

void *gc_alloc(size_t size) {
  size = MAX(8, ALIGN8(size));

  // check if we need to garbage collect
  if (heap_next + sizeof(size_t) + size > heap_size) {
    panic("We can't run garbage collection yet!");
  }

  // allocate the block
  size_t *block = (size_t*) (heap + heap_next);
  heap_next += sizeof(size_t) + size;
  *block = size;
  return (void*) (block + 1);
}

void *gc_realloc(void *addr, size_t newsize) {
  // TODO - eventually extend the current block if it's at the end of the heap
  size_t oldsize = *(((size_t*) addr) - 1);
  void *newblock = gc_alloc(newsize);
  memcpy(newblock, addr, oldsize);
  return newblock;
}

void gc_free(void *addr) {
  // TODO - if it's at the end of the heap shrink the heap
  // The whole point of garbage collection is to not have a free function, but
  // there are times when we know we will never use something that we just
  // allocated (but still have to allocate somewhere), so in those cases it
  // kind of makes sense to have an 'undo' operation. This will only work if
  // the block is at the very end of the heap.
  addr = addr;
}

void garbage_collect(luav *start, lstack_t *init_stack, lframe_t *running) {
  start = start;
  init_stack = init_stack;
  running = running;
}
