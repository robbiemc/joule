#include <string.h>
#include <sys/mman.h>

#include "config.h"
#include "lib/coroutine.h"
#include "luav.h"
#include "panic.h"
#include "gc.h"

#define HEAP1_ADDR ((void*) 0x0000600000000000)
#define HEAP2_ADDR ((void*) 0x0000700000000000)
#define HEAP2_END  ((void*) 0x0000800000000000)

#define ALIGN8(n) (((n) + 7) & (~(size_t)7))
#define ENOUGH_SPACE(size) (heap_next + sizeof(size_t) + (size) < heap_size)
#define GC_MOVED(size) ((size) & 1)
#define GC_SETMOVED(sizep, newp) ({*(sizep) |= 1; *((sizep)+1) = (size_t)(newp);})
#define IN_HEAP(p) ((void*)(p) > HEAP1_ADDR && (void*)(p) < HEAP2_END)

static void *heap;
static size_t heap_size;
static size_t heap_next;
static int initialized = FALSE;

static void gc_mmap(void *heap, size_t amount, size_t offset);
static void gc_resize(size_t needed);
static luav gc_traverse(luav val);
static void gc_traverse_stack(lstack_t *stack);

INIT static void gc_init() {
  // allocate two heaps
  heap_size = INIT_HEAP_SIZE;
  heap_next = 0;
  heap = HEAP1_ADDR;
  gc_mmap(heap, heap_size, 0);
  initialized = TRUE;
}

DESTROY static void gc_destroy() {
  xassert(munmap(heap, heap_size) == 0);
}

static void gc_mmap(void *heap, size_t amount, size_t offset) {
  void *addr = heap + offset;
  xassert(mmap(addr, amount, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0) == addr);
}

void *gc_calloc(size_t nelt, size_t eltsize) {
  size_t size = nelt * eltsize;
  size = ALIGN8(size);

  void *addr = gc_alloc(size);
  memset(addr, 0, size);
  return addr;
}

void *gc_alloc(size_t size) {
  xassert(initialized == TRUE && "Garbage collector not initialized");
  size = MAX(8, ALIGN8(size));

  // check if we need to garbage collect
  if (!ENOUGH_SPACE(size)) {
    garbage_collect();
    if (!ENOUGH_SPACE(size)) {
      // garbage collection didn't help - resize the heap
      gc_resize(size);
    }
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

static void gc_resize(size_t needed) {
  static const size_t meg = 1024 * 1024 - 1;
  size_t size = (needed + meg) & (~meg); // round up to nearest meg
  size = MAX(size, heap_size);
  gc_mmap(heap, size, heap_size);
  heap_size += size;
}

void garbage_collect() {
  /*
    extern struct lhash userdata_meta;
    extern struct lhash lua_globals;
    extern struct lhash *global_env;
    extern lframe_t *vm_running;
    extern lstack_t *vm_stack;
    extern lstack_t init_stack;
   */

  size_t old_size = heap_next;
  // make another heap
  void *old = heap;
  heap = (heap == HEAP1_ADDR ? HEAP2_ADDR : HEAP1_ADDR);
  gc_mmap(heap, heap_size, 0);
  heap_next = 0;

  gc_traverse_stack(&init_stack);
  if (vm_stack != &init_stack)
    gc_traverse_stack(vm_stack);

  gc_traverse(lv_table(&userdata_meta));
  gc_traverse(lv_table(&lua_globals));
  global_env = lv_gettable(gc_traverse(lv_table(global_env)), 0);
  
  lframe_t *frame;
  for (frame = vm_running; frame != NULL; frame = frame->caller) {
    frame->closure = lv_getfunction(gc_traverse(lv_function(frame->closure)),0);
  }

  printf("Garbage Collected: %zu -> %zu\n", old_size, heap_next);

  munmap(old, heap_size);
}

static luav gc_traverse(luav val) {
  printf("gc_traverse with type %d\n", lv_gettype(val));
  switch (lv_gettype(val)) {
    case LSTRING: {
      lstring_t *str = lv_getptr(val);
      if (str->type == LSTR_TYPE_MALLOC)
        return val;
      size_t *bsize = ((size_t*) str) - 1;
      if (GC_MOVED(*bsize))
        return lv_string(*((lstring_t**) str));

      lstring_t *str2 = lstr_alloc(str->length);
      memcpy(str2->data, str->data, str->length + 1);
      GC_SETMOVED(bsize, str2);
      return lv_string(str2);
    }

    case LTABLE: {
      lhash_t *hash = lv_getptr(val);
      if (!IN_HEAP(hash))
        return val;
      size_t *bsize = ((size_t*) hash) - 1;
      if (GC_MOVED(*bsize))
        return lv_table(*((lhash_t**) hash));

      size_t i;
      lhash_t *hash2 = gc_alloc(sizeof(lhash_t));
      memcpy(hash2, hash, sizeof(lhash_t));
      luav *array = hash->array;
      GC_SETMOVED(bsize, hash2);
      hash2->metatable = lv_gettable(gc_traverse(lv_table(hash2->metatable)), 0);
      // copy over the array
      hash2->array = gc_alloc(hash2->acap * sizeof(luav));
      for (i = 0; i < hash2->acap; i++)
        hash2->array[i] = gc_traverse(array[i]);
      // reinsert things into the hashtable
      hash2->tsize = 0;
      hash2->table = gc_alloc(hash2->tcap * sizeof(struct lh_pair));
      for (i = 0; i < hash2->tcap; i++) {
        struct lh_pair *entry = &hash->table[i];
        if (entry->key != LUAV_NIL)
          lhash_set(hash2, entry->key, entry->value);
      }
      return lv_table(hash2);
    }

    case LFUNCTION: {
      lclosure_t *func = lv_getptr(val);
      size_t *bsize = ((size_t*) func) - 1;
      if (GC_MOVED(*bsize))
        return lv_function(*((lclosure_t**) func));
      if (func->type == LUAF_C)
        return val;

      size_t num_upvalues = func->function.lua->num_upvalues;
      size_t func_size = sizeof(lclosure_t) + (num_upvalues - 1) * sizeof(luav);
      lclosure_t *func2 = gc_alloc(func_size);
      memcpy(func2, func, sizeof(lclosure_t));
      GC_SETMOVED(bsize, func2);
      func2->env = lv_gettable(gc_traverse(lv_table(func2->env)), 0);
      size_t i;
      for (i = 0; i < num_upvalues; i++)
        func2->upvalues[i] = gc_traverse(func2->upvalues[i]);
      return lv_function(func2);
    }

    case LTHREAD: {
      lthread_t *thread = lv_getptr(val);
      size_t *bsize = ((size_t*) thread) - 1;
      if (GC_MOVED(*bsize))
        return lv_thread(*((lthread_t**) thread));

      lthread_t *thread2 = gc_alloc(sizeof(lthread_t));
      memcpy(thread2, thread, sizeof(lthread_t));
      GC_SETMOVED(bsize, thread2);
      thread2->caller = lv_getthread(gc_traverse(lv_thread(thread2->caller)), 0);
      thread2->closure = lv_getfunction(gc_traverse(lv_function(thread2->closure)), 0);
      thread2->env = lv_gettable(gc_traverse(lv_table(thread2->env)), 0);
      gc_traverse_stack(&thread2->vm_stack);
      return lv_thread(thread2);
    }

    case LUPVALUE: {
      luav *ptr = lv_getptr(val);
      size_t *bsize = ((size_t*) ptr) - 1;
      if (GC_MOVED(*bsize))
        return lv_upvalue(*((luav**) ptr));

      luav *ptr2 = gc_alloc(sizeof(luav));
      luav upv = *ptr;
      GC_SETMOVED(bsize, ptr2);
      *ptr2 = gc_traverse(upv);
      return lv_upvalue(ptr2);
    }

    case LNUMBER:
    case LBOOLEAN:
    case LNIL:
    case LUSERDATA:
    default:
      return val;
  }
  panic("Shouldn't be here");
}

static void gc_traverse_stack(lstack_t *stack) {
  // the stack itself doesn't get copied - it's either statically allocated or
  // part of an lthread_t

  luav *val;
  for (val = stack->base; val != stack->top; val++)
    *val = gc_traverse(*val);
}
