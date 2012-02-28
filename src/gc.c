/**
 * @file gc.c
 * @brief Implementation of Garbage Collection
 *
 * Currently this is implemented as a mark and sweep garbage collector
 */
#include <assert.h>
#include <string.h>
#include <sys/mman.h>

#include "arch.h"
#include "config.h"
#include "lib/coroutine.h"
#include "luav.h"
#include "panic.h"
#include "gc.h"

#define GC_HOOKS 50
#define GC_NEXT(quad) ((void*) ((quad) & 0x00fffffffffffc))
#define GC_TYPE(quad) ((int) (((quad) >> 56) & 0xff))
#define GC_BUILD(next, type) ((u64) (next) | (((u64) (type)) << 56))
#define GC_ISBLACK(ptr) (*((u64*) (ptr) - 1) & 1)
#define GC_SETBLACK(ptr) (*((u64*) (ptr) - 1) |= 1)

/* Heap metadata */
static size_t heap_limit = INIT_HEAP_SIZE;
static size_t heap_size  = 0;
static size_t heap_last  = 0;
static int    gc_paused  = FALSE;
static u64   *gc_head    = NULL;

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

void gc_pause() {
  gc_paused = 1;
}

void gc_unpause() {
  gc_paused = 0;
}

void *gc_calloc(size_t nelt, size_t eltsize, int type) {
  size_t size = nelt * eltsize;
  void *addr = gc_alloc(size, type);
  memset(addr, 0, size);
  return addr;
}

void *gc_alloc(size_t size, int type) {
  size += sizeof(u64);

  /* check if we need to garbage collect */
  if (!gc_paused && heap_size + size >= heap_limit) {
    garbage_collect();
    heap_last = heap_size;
    /* Adjust the heap limit as necessary */
    if (heap_size + size >= heap_limit) {
      heap_limit *= 2;
    } else if ((heap_size + size) * 2 < heap_limit) {
      heap_limit /= 2;
    }
  }
  heap_size += size;
  assert(heap_size < heap_limit);

  /* allocate the block */
  u64 *block = malloc(size);
  xassert(block != NULL);
  *block = GC_BUILD(gc_head, type);
  gc_head = block;
  return (void*) (block + 1);
}

void *gc_realloc(void *_addr, size_t newsize) {
  u64 *addr = ((u64*) _addr) - 1;
  u64 header = *addr;
  u64 *addr2 = realloc(addr, newsize + sizeof(u64));
  xassert(addr2 != NULL);
  if (addr2 != addr) {
    /* Find our location in the list, and update our "previous" pointer */
    if (gc_head == addr) {
      gc_head = addr2;
    } else {
      u64 *prev = gc_head;
      u64 *cur = gc_head;
      while (cur != addr) {
        prev = cur;
        cur = GC_NEXT(*cur);
      }
      *prev = GC_BUILD(addr2, GC_TYPE(*addr));
    }
    /* Now fix our "next" pointer */
    *addr2 = header;
  }
  return addr2 + 1;
}

/**
 * @brief Actually run garbage collection
 */
void garbage_collect() {
  /* Sanity check to make sure we don't GC in GC */
  static int in_gc = 0;
  assert(!in_gc);
  in_gc = 1;

  /* Run all hooks */
  int i;
  for (i = 0; i < num_hooks; i++) {
    gc_hooks[i]();
  }

  u64 *to_finalize = NULL;
  u64 *cur = gc_head;
  gc_head = NULL;

  /* Move everything in the current list onto one of gc_head or the to_finalize
     list */
  while (cur != NULL) {
    u64 header = *cur;
    if (GC_ISBLACK(cur + 1)) {
      *cur = GC_BUILD(gc_head, GC_TYPE(header));
      gc_head = cur;
    } else {
      *cur = GC_BUILD(to_finalize, GC_TYPE(header));
      to_finalize = cur;
    }
    cur = GC_NEXT(header);
  }

  while (to_finalize != NULL) {
    u64 header = *to_finalize;
    int type = GC_TYPE(header);
    if (type == LSTRING) {
      lstr_remove((lstring_t*) (to_finalize + 1));
    } else if (type == LTHREAD) {
      coroutine_free((lthread_t*) (to_finalize + 1));
    }
    free(to_finalize);
    to_finalize = GC_NEXT(header);
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
  if (_ptr == NULL || (_ptr > exec_edata() && GC_ISBLACK(_ptr))) {
    return;
  } else if (_ptr > exec_edata()) {
    GC_SETBLACK(_ptr);
  }

  switch (type) {
    case LSTRING:
      break; /* no recursion */

    case LTABLE: {
      lhash_t *hash = _ptr;
      if (GC_ISBLACK(hash->array)) {
        assert(GC_ISBLACK(hash->table));
        break;
      }
      GC_SETBLACK(hash->array);
      GC_SETBLACK(hash->table);

      size_t i;
      gc_traverse_pointer(hash->metatable, LTABLE);
      /* copy over the array */
      if (hash->array != NULL) {
        for (i = 0; i < hash->length; i++) {
          gc_traverse(hash->array[i]);
        }
      }
      /* reinsert hash into the hashtable */
      if (hash->table != NULL) {
        for (i = 0; i < hash->tcap; i++) {
          if (hash->table[i].key != LUAV_NIL) {
            gc_traverse(hash->table[i].key);
            gc_traverse(hash->table[i].value);
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
      panic("not a pointer");
  }
}

void gc_traverse_stack(lstack_t *stack) {
  luav *val;
  for (val = stack->base; val != stack->top; val++) {
    gc_traverse(*val);
  }
}
