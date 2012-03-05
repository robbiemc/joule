/**
 * @file gc.c
 * @brief Implementation of Garbage Collection
 *
 * Currently this is implemented as a mark and sweep garbage collector
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "arch.h"
#include "config.h"
#include "lib/coroutine.h"
#include "luav.h"
#include "panic.h"
#include "gc.h"

#define GC_HOOKS 50
#define GC_PERM_BIT ((u64)2)
#define GC_ADDR_MASK UINT64_C(0x00fffffffffffc)
#define GC_NEXT(header) ((void*) (size_t) ((header)->bits & GC_ADDR_MASK))
#define GC_TYPE(header) ((int) (((header)->bits >> 56) & 0xff))
#define GC_BUILD(next, type, bit) ((u64) (size_t) (next) | \
                                   (((u64) (type)) << 56) | (bit))
#define GC_ISBLACK(ptr) (((gc_header_t*) (ptr) - 1)->bits & 1)
#define GC_ISPERM(ptr) (((gc_header_t*) (ptr) - 1)->bits & 2)
#define GC_SETBLACK(ptr) (((gc_header_t*) (ptr) - 1)->bits |= 1)

typedef struct gc_header {
  u64 bits;
  size_t size;
} gc_header_t;

/* Heap metadata */
static size_t heap_limit = INIT_HEAP_SIZE;
static size_t heap_size  = 0;
static size_t heap_last  = 0;
static gc_header_t *gc_head = NULL;

/* Hook stuff */
static gchook_t* gc_hooks[GC_HOOKS];
static int num_hooks = 0;

/**
 * @brief Ultimately garbage collect by blowing away the entire heap
 */
DESTROY static void gc_destroy() {
  num_hooks = 0;
  garbage_collect();
}

/**
 * @brief Add a hook to be run at the garbage collection time
 *
 * This is meant to be used to update things like static or global variables
 * which would otherwise require pairs of setters/getters to update all over the
 * place. This way, it's a burden on each module to maintain its global
 * variables, and less work for the garbage collector.
 *
 * GC functions are provided in the header, namely gc_traverse() and
 * gc_traverse_pointer()
 *
 * @param hook the function to run at GC-time
 */
void gc_add_hook(gchook_t *hook) {
  assert(num_hooks + 1 < GC_HOOKS);
  gc_hooks[num_hooks++] = hook;
}

void *gc_alloc(size_t size, int type) {
  size += sizeof(gc_header_t);

  /* check if we need to garbage collect */
  if (heap_size + size >= heap_limit) {
    garbage_collect();
    heap_last = heap_size;
    /* Adjust the heap limit as necessary */
    if (heap_size + size >= heap_limit) {
      heap_limit += MAX(heap_limit, heap_size + size - heap_limit + 1024);
    } else if ((heap_size + size) * 2 < heap_limit &&
               heap_limit > INIT_HEAP_SIZE) {
      heap_limit /= 2;
    }
  }
  heap_size += size;
  assert(heap_size < heap_limit);

  /* allocate the block */
  gc_header_t *block = xmalloc(size);
  block->bits = GC_BUILD(gc_head, type, GC_PERM_BIT);
  block->size = size;
  gc_head = block;
  return block + 1;
}

void *gc_realloc(void *_addr, size_t newsize) {
  newsize += sizeof(gc_header_t);
  gc_header_t *addr = ((gc_header_t*) _addr) - 1;
  u64 bits = addr->bits;
  heap_size += newsize - addr->size;
  gc_header_t *addr2 = xrealloc(addr, newsize);
  if (addr2 != addr) {
    /* Find our location in the list, and update our "previous" pointer */
    if (gc_head == addr) {
      gc_head = addr2;
    } else {
      gc_header_t *prev = gc_head;
      gc_header_t *cur = gc_head;
      while (cur != addr) {
        prev = cur;
        cur = GC_NEXT(cur);
      }
      prev->bits = GC_BUILD(addr2, GC_TYPE(prev), GC_ISPERM(prev + 1));
    }
    /* Now fix our "next" pointer */
    addr2->bits = bits;
  }
  addr2->size = newsize;
  return addr2 + 1;
}

/**
 * @brief Releases a pointer
 *
 * Sets something as garbage-collectable
 */
void gc_release(void *_addr) {
  gc_header_t *addr = ((gc_header_t*) _addr) - 1;
  addr->bits &= ~GC_PERM_BIT;
}

/**
 * @brief Actually run garbage collection
 */
void garbage_collect() {
  /* Sanity check to make sure we don't GC in GC */
  static int in_gc = 0;
  xassert(!in_gc);
  in_gc = 1;

  /* Run all hooks */
  int i;
  for (i = 0; i < num_hooks; i++) {
    gc_hooks[i]();
  }

  gc_header_t *cur = gc_head;
  gc_header_t *nxt;

  /* Traverse all permanent objects */
  while (cur != NULL && num_hooks != 0) {
    if (GC_ISPERM(cur + 1)) {
      gc_traverse_pointer(cur + 1, GC_TYPE(cur));
    }
    cur = GC_NEXT(cur);
  }

  /* Move everything in the current list onto one of gc_head or the to_finalize
     list */
  cur = gc_head;
  gc_head = NULL;
  while (cur != NULL) {
    nxt = GC_NEXT(cur);
    if (GC_ISBLACK(cur + 1)) {
      cur->bits = GC_BUILD(gc_head, GC_TYPE(cur), GC_ISPERM(cur + 1));
      gc_head = cur;
    } else {
      switch (GC_TYPE(cur)) {
        case LSTRING:
          lstr_remove((lstring_t*) (cur + 1));
          break;
        case LTHREAD:
          coroutine_free((lthread_t*) (cur + 1));
          break;
      }
      heap_size -= cur->size;
      assert((ssize_t) heap_size >= 0);
      memset(cur, 0x42, cur->size);
      free(cur);
    }
    cur = nxt;
  }

  in_gc = 0;
}

/**
 * @brief Traverse a lua value during garbage collection, updating all pointers
 *        as necessary
 *
 * @param val the value to traverse
 * @return the new value that replaces the given one
 */
void gc_traverse(luav val) {
  int type = lv_gettype(val);
  switch (type) {
    case LSTRING:
    case LTABLE:
    case LFUNCTION:
    case LTHREAD:
    case LUPVALUE:
      gc_traverse_pointer(lv_getptr(val), type);
      break;

    case LFUNC:
    case LCFUNC:
      panic("bad item to gc_traverse");

    case LNUMBER:
    case LBOOLEAN:
    case LNIL:
    case LUSERDATA:
      break;

    default:
      panic("bad type in gc: %d\n", type);
  }
}

/**
 * @brief Traverses a pointer of the given type, recursing depending on the
 *        type
 *
 * @param _ptr the pointer to a block of memory (possibly not in the heap)
 * @param type the type of the pointer
 * @return the new location of the pointer, possibly the same value
 */
void gc_traverse_pointer(void *_ptr, int type) {
  if (_ptr == NULL || GC_ISBLACK(_ptr)) {
    return;
  }
  GC_SETBLACK(_ptr);

  switch (type) {
    case LSTRING:
    case LANY:
    case LCFUNC:
      break; /* no need for recursion */

    case LTABLE: {
      lhash_t *hash = _ptr;
      size_t i;
      gc_traverse_pointer(hash->metatable, LTABLE);
      /* copy over the array */
      if (hash->array != NULL) {
        GC_SETBLACK(hash->array);
        for (i = 0; i < hash->length; i++) {
          gc_traverse(hash->array[i]);
        }
      }
      /* reinsert hash into the hashtable */
      if (hash->table != NULL) {
        GC_SETBLACK(hash->table);
        for (i = 0; i < hash->tcap; i++) {
          if (hash->table[i].key != LUAV_NIL) {
            gc_traverse(hash->table[i].key);
            gc_traverse(hash->table[i].value);
          } else {
            hash->table[i].value = LUAV_NIL;
          }
        }
      }
      break;
    }

    /* Functions have upvalues, environments, and lfunc_ts possibly */
    case LFUNCTION: {
      lclosure_t *func = _ptr;
      int upvalues = func->type == LUAF_LUA ?
                            func->function.lua->num_upvalues :
                            func->function.c->upvalues;
      gc_traverse_pointer(func->env, LTABLE);
      int i;
      for (i = 0; i < upvalues; i++) {
        gc_traverse(func->upvalues[i]);
      }
      if (func->type == LUAF_LUA) {
        gc_traverse_pointer(func->function.lua, LFUNC);
      } else if (func->type == LUAF_C){
        gc_traverse_pointer(func->function.c, LCFUNC);
      }
      break;
    }

    /* Make sure all thread fields stick around, mainly the thread's stack */
    case LTHREAD: {
      lthread_t *thread = _ptr;
      gc_traverse_pointer(thread->caller, LTHREAD);
      gc_traverse_pointer(thread->closure, LFUNCTION);
      gc_traverse_pointer(thread->env, LTABLE);
      gc_traverse_stack(&thread->vm_stack);
      break;
    }

    /* Keep around the upvalue, and travel through */
    case LUPVALUE: {
      luav *ptr = _ptr;
      gc_traverse(*ptr);
      break;
    }

    /* Make sure we keep around all constants and nested functions */
    case LFUNC: {
      lfunc_t *func = _ptr;
      gc_traverse_pointer(func->name, LSTRING);
      if (func->instrs != NULL) GC_SETBLACK(func->instrs);
      if (func->consts != NULL) GC_SETBLACK(func->consts);
      if (func->funcs != NULL)  GC_SETBLACK(func->funcs);
      if (func->lines != NULL)  GC_SETBLACK(func->lines);
      u32 i;
      for (i = 0; i < func->num_consts; i++) {
        gc_traverse(func->consts[i]);
      }
      for (i = 0; i < func->num_funcs; i++) {
        gc_traverse_pointer(func->funcs[i], LFUNC);
      }
      break;
    }

    case LNUMBER:
    case LBOOLEAN:
    case LNIL:
    case LUSERDATA:
    default:
      panic("not a pointer type: %d", type);
  }

}

void gc_traverse_stack(lstack_t *stack) {
  u32 i;
  for (i = 0; i < stack->size; i++) {
    gc_traverse(stack->base[i]);
  }
}
