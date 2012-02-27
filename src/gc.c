#include <string.h>
#include <sys/mman.h>

#include "arch.h"
#include "config.h"
#include "lib/coroutine.h"
#include "luav.h"
#include "panic.h"
#include "gc.h"

#define HEAP1_ADDR ((void*) 0x0000500000000000)
#define HEAP2_ADDR ((void*) 0x0000600000000000)

#define ALIGN8(n) (((n) + 7) & (~(size_t)7))
#define ENOUGH_SPACE(size) (heap_next + sizeof(size_t) + (size) < heap_size)
#define GC_MOVED(size) ((size) & 1)
#define GC_SETMOVED(blockp, newp)      \
  do {                                 \
    size_t *ptr = (size_t*) (blockp);  \
    *(ptr - 1) |= 1;                   \
    *ptr = (size_t) (newp);            \
  } while (0)
#define IN_HEAP(p, h) ((void*)(p) > (h) && (void*)(p) < ((h) + heap_size))

#define GCH_MAGIC_TAG  ((size_t) 0x93a7)
#define GCH_SIZE(quad) ((quad) & 0xffffffffff)
#define GCH_TYPE(quad) ((int) (((quad) >> 56) & 0xff))
#define GCH_TAG(quad)  (((quad) >> 40) & 0xffff)
#define GCH_BUILD(size, type) \
  ((size_t) ((size) & 0xffffffffff) | (((size_t) (type)) << 56) | \
   (GCH_MAGIC_TAG << 40))

static void *heap;
static size_t heap_size;
static size_t heap_next;
static int initialized = FALSE;
static void *stack_bottom;
static void *stack_top;

static void gc_mmap(void *heap, size_t amount, size_t offset);
static void gc_resize(size_t needed);
static luav gc_traverse(luav val);
static void *gc_traverse_pointer(void *_ptr, int type);
static void gc_traverse_stack(lstack_t *stack);
static void traverse_the_stack_oh_god(void *old_bottom, void *old_top);

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

void gc_set_bottom() {
  stack_bottom = get_sp();
}

static void gc_mmap(void *heap, size_t amount, size_t offset) {
  void *addr = heap + offset;
  xassert(mmap(addr, amount, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0) == addr);
}

void *gc_calloc(size_t nelt, size_t eltsize, int type) {
  size_t size = nelt * eltsize;
  size = ALIGN8(size);

  void *addr = gc_alloc(size, type);
  memset(addr, 0, size);
  return addr;
}

void *gc_alloc(size_t size, int type) {
  xassert(initialized == TRUE && "Garbage collector not initialized");
  size = MAX(8, ALIGN8(size));

  // check if we need to garbage collect
  if (!ENOUGH_SPACE(size)) {
    stack_top = get_sp();
    garbage_collect();
    if (!ENOUGH_SPACE(size)) {
      // garbage collection didn't help - resize the heap
      gc_resize(size);
    }
  }
  xassert(ENOUGH_SPACE(size));
  // allocate the block
  size_t *block = (size_t*) (heap + heap_next);
  heap_next += sizeof(size_t) + size;
  *block = GCH_BUILD(size, type);
  return (void*) (block + 1);
}

void *gc_realloc(void *addr, size_t newsize) {
  // TODO - eventually extend the current block if it's at the end of the heap
  size_t oldheader = *(((size_t*) addr) - 1);
  void *newblock = gc_alloc(newsize, GCH_TYPE(oldheader));
  memcpy(newblock, addr, GCH_SIZE(oldheader));
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
  static int in_gc = 0;
  xassert(!in_gc);
  in_gc = 1;
  size_t old_size = heap_next;
  // make another heap
  void *old = heap;
  heap = (heap == HEAP1_ADDR ? HEAP2_ADDR : HEAP1_ADDR);
  gc_mmap(heap, heap_size, 0);
  heap_next = 0;

  gc_traverse_stack(&init_stack);
  if (vm_stack != &init_stack)
    gc_traverse_stack(vm_stack);

  gc_traverse_pointer(&userdata_meta, LTABLE);
  gc_traverse_pointer(&lua_globals, LTABLE);
  global_env = gc_traverse_pointer(global_env, LTABLE);

  lframe_t *frame;
  for (frame = vm_running; frame != NULL; frame = frame->caller) {
    frame->closure = gc_traverse_pointer(frame->closure, LFUNCTION);
  }

  traverse_the_stack_oh_god(old, (char*) old + old_size);

  lstr_gc();

  munmap(old, heap_size);
  in_gc = 0;
}

static luav gc_traverse(luav val) {
  switch (lv_gettype(val)) {
    case LSTRING:
      return lv_string(gc_traverse_pointer(lv_getptr(val), LSTRING));
    case LTABLE:
      return lv_table(gc_traverse_pointer(lv_getptr(val), LTABLE));
    case LFUNCTION:
      return lv_function(gc_traverse_pointer(lv_getptr(val), LFUNCTION));
    case LTHREAD:
      return lv_thread(gc_traverse_pointer(lv_getptr(val), LTHREAD));
    case LUPVALUE:
      return lv_upvalue(gc_traverse_pointer(lv_getptr(val), LUPVALUE));
  }
  return val;
}

/**
 * @brief Traverses a pointer of the given type, recursing depending on the
 *        type
 *
 * @param _ptr the pointer to a block of memory (possibly not in the heap)
 * @param type the type of the pointer
 * @return the new location of the pointer, possibly the same value
 */
static void *gc_traverse_pointer(void *_ptr, int type) {
  if (_ptr == NULL) return NULL;
  /* If a pointer is in our heap, and it's been moved, we can quickly return
     where it's been moved to */
  size_t *bsize = ((size_t*) _ptr) - 1;
  void *old = heap == HEAP1_ADDR ? HEAP2_ADDR : HEAP1_ADDR;
  if (IN_HEAP(_ptr, old) && GC_MOVED(*bsize)) {
    void *other = *((void**) _ptr);
    return other;
  }
  xassert(!IN_HEAP(_ptr, heap));

  switch (type) {
    case LSTRING: {
      lstring_t *str = _ptr;
      if (!IN_HEAP(_ptr, old)) {
        xassert(str->type == LSTR_TYPE_MALLOC);
        return _ptr;
      }
      xassert(str->type != LSTR_TYPE_MALLOC);

      lstring_t *str2 = lstr_alloc(str->length);
      memcpy(str2->data, str->data, str->length + 1);
      str2->length = str->length;
      str2->hash = str->hash;
      GC_SETMOVED(_ptr, str2);
      return str2;
    }

    case LTABLE: {
      lhash_t *hash = _ptr;
      /* Heap-allocated hashes need to be moved */
      if (IN_HEAP(_ptr, old)) {
        hash = gc_alloc(sizeof(lhash_t), LTABLE);
        memcpy(hash, _ptr, sizeof(lhash_t));
        GC_SETMOVED(_ptr, hash);
      /* Statically allocated hashes whose arrays have been moved have already
         been traversed */
      } else if (!IN_HEAP(hash->array, old)) {
        xassert(IN_HEAP(hash->array, heap));
        xassert(IN_HEAP(hash->table, heap));
        return hash;
      }

      size_t i;
      hash->metatable = gc_traverse_pointer(hash->metatable, LTABLE);
      /* copy over the array */
      if (hash->array != NULL) {
        luav *array = gc_alloc(hash->acap * sizeof(luav), LANY);
        for (i = 0; i < hash->acap; i++) {
          array[i] = gc_traverse(hash->array[i]);
        }
        GC_SETMOVED(hash->array, array);
        hash->array = array;
      }
      /* reinsert hash into the hashtable */
      if (hash->table != NULL) {
        struct lh_pair *table = hash->table;
        hash->table = gc_alloc(hash->tcap * sizeof(struct lh_pair), LANY);
        for (i = 0; i < hash->tcap; i++) {
          hash->table[i].key = hash->table[i].value = LUAV_NIL;
        }
        hash->tsize = 0;
        /* Flag changes necessary? */
        /* TODO: put this in lhash.c */
        hash->flags |= LHASH_RESIZING;
        for (i = 0; i < hash->tcap; i++) {
          if (table[i].key != LUAV_NIL && table[i].value != LUAV_NIL) {
            lhash_set(hash, gc_traverse(table[i].key),
                      gc_traverse(table[i].value));
          }
        }
        hash->flags &= ~LHASH_RESIZING;
        GC_SETMOVED(table, hash->table);
      }
      return hash;
    }

    /* Functions have upvalues, environments, and lfunc_ts possibly */
    case LFUNCTION: {
      size_t upvalues = 0;
      lclosure_t *func = _ptr;
      /* Move things if they're in the heap */
      if (IN_HEAP(_ptr, old)) {
        size_t size = GCH_SIZE(*bsize);
        upvalues = (size - sizeof(lclosure_t)) / sizeof(luav);
        xassert(CLOSURE_SIZE(upvalues) == size);
        func = gc_alloc(size, LFUNCTION);
        memcpy(func, _ptr, size);
        GC_SETMOVED(_ptr, func);
      }

      func->env = gc_traverse_pointer(func->env, LTABLE);
      size_t i;
      for (i = 0; i < upvalues; i++) {
        func->upvalues[i] = gc_traverse(func->upvalues[i]);
      }

      if (func->type == LUAF_LUA) {
        func->function.lua = gc_traverse_pointer(func->function.lua, LFUNC);
      }

      return func;
    }

    /* A thread just needs to copy its metadata over to the new object */
    case LTHREAD: {
      lthread_t *other = gc_alloc(sizeof(lthread_t), LTHREAD);
      memcpy(other, _ptr, sizeof(lthread_t));
      GC_SETMOVED(_ptr, other);
      other->caller  = gc_traverse_pointer(other->caller, LTHREAD);
      other->closure = gc_traverse_pointer(other->closure, LFUNCTION);
      other->env     = gc_traverse_pointer(other->env, LTABLE);
      gc_traverse_stack(&other->vm_stack);
      return other;
    }

    /* Upvalues only need the pointed to value to be transferred over */
    case LUPVALUE: {
      luav *ptr = _ptr;
      luav *ptr2 = gc_alloc(sizeof(luav), LUPVALUE);
      luav upv = *ptr;
      GC_SETMOVED(_ptr, ptr2);
      *ptr2 = gc_traverse(upv);
      return ptr2;
    }

    /* lfunc_t's have a lot of allocated fields that need to be copied over,
       and a few need to be recursed into */
    case LFUNC: {
      u32 i;
      lfunc_t *other = gc_alloc(sizeof(lfunc_t), LFUNC);
      memcpy(other, _ptr, sizeof(lfunc_t));
      GC_SETMOVED(_ptr, other);
      other->name = gc_traverse_pointer(other->name, LSTRING);
      /* Instructions */
      u64 size = sizeof(u32) * other->num_instrs;
      u32 *instrs = gc_alloc(size, LANY);
      memcpy(instrs, other->instrs, size);
      GC_SETMOVED(other->instrs, instrs);
      other->instrs = instrs;
      /* Constants */
      size = sizeof(luav) * other->num_consts;
      luav *consts = gc_alloc(size, LANY);
      for (i = 0; i < other->num_consts; i++) {
        consts[i] = gc_traverse(other->consts[i]);
      }
      GC_SETMOVED(other->consts, consts);
      other->consts = consts;
      /* Functions */
      size = sizeof(lfunc_t) * other->num_funcs;
      if (other->funcs != NULL) {
        lfunc_t **funcs = gc_alloc(size, LANY);
        for (i = 0; i < other->num_funcs; i++) {
          funcs[i] = gc_traverse_pointer(other->funcs[i], LFUNC);
        }
        GC_SETMOVED(other->funcs, funcs);
        other->funcs = funcs;
      }
      /* Lines */
      size = sizeof(u32) * other->num_lines;
      u32 *lines = gc_alloc(size, LANY);
      memcpy(lines, other->lines, size);
      GC_SETMOVED(other->lines, lines);
      other->lines = lines;
      return other;
    }

    case LNUMBER:
    case LBOOLEAN:
    case LNIL:
    case LUSERDATA:
    default:
      panic("invalid call");
      return _ptr;
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

static void think_about_a_pointer(size_t *loc, size_t _ptr, int is_luav) {
  size_t *ptr = (size_t*) _ptr;
  size_t header = *(ptr - 1);
  if (GCH_TAG(header) != GCH_MAGIC_TAG || GCH_TYPE(header) == LANY) {
    size_t *object = (size_t*) ((size_t)ptr & ~(size_t)7);
    while (GCH_TAG(*(object - 1)) != GCH_MAGIC_TAG) {
      object--;
    }
    header = *(object - 1);
    if (GCH_TYPE(header) != LANY) return;
    if (_ptr >= (size_t) object + GCH_SIZE(header)) return;
    if (!GC_MOVED(header)) return;
    xassert(GCH_TYPE(header) == LANY);
    xassert(_ptr <= (size_t) object + GCH_SIZE(header));
    xassert(GC_MOVED(header));
    size_t diff = *object - (size_t) object;
    *loc += diff;
    return;
  }

  if ((_ptr & 0x7)) return;
  switch (GCH_TYPE(header)) {
    case LUPVALUE:  *loc = gc_traverse(lv_upvalue(ptr));  break;
    case LTHREAD:   *loc = gc_traverse(lv_thread(ptr));   break;
    case LFUNCTION: *loc = gc_traverse(lv_function(ptr)); break;
    case LSTRING:   *loc = gc_traverse(lv_string(ptr));   break;
    case LTABLE:    *loc = gc_traverse(lv_table(ptr));    break;
    case LFUNC:
      xassert(!is_luav);
      *loc = (size_t) gc_traverse_pointer(ptr, LFUNC);
      break;
    default:
      if (GC_MOVED(header)) {
        xassert(!is_luav);
        *loc = *ptr;
        break;
      }
      panic("what do we do now?!");
  }
  if (!is_luav) {
    *loc = (size_t) lv_getptr(*loc);
  }
}

static void traverse_the_stack_oh_god(void *old_bot, void *old_top) {
  size_t *stack;
  size_t heap_top = (size_t) old_top;
  size_t heap_bot = (size_t) old_bot;

  for (stack = stack_top; stack < (size_t*) stack_bottom; stack++) {
    size_t tmp = *stack;
    if (tmp < heap_bot + sizeof(size_t) || tmp > heap_top) {
      switch (lv_gettype(tmp)) {
        case LUPVALUE:
        case LTHREAD:
        case LSTRING:
        case LFUNCTION:
        case LTABLE:
          tmp = (size_t) lv_getptr(tmp);
          if (tmp >= heap_bot + sizeof(size_t) && tmp <= heap_top) {
            think_about_a_pointer(stack, tmp, TRUE);
          }
      }
      continue;
    }
    think_about_a_pointer(stack, tmp, FALSE);
  }
}

/**
 * @brief After GC, but before heap removal, given an address in the old heap,
 *        returns the new location of the block of memory, or NULL if the
 *        memory is deallocated
 */
void *gc_relocate(void *_addr) {
  void *old = heap == HEAP1_ADDR ? HEAP2_ADDR : HEAP1_ADDR;
  if (!IN_HEAP(_addr, old)) {
    return _addr;
  }
  size_t *addr = (size_t*) _addr;
  size_t header = *(addr - 1);
  if (GC_MOVED(header))
    return (void*) *addr;
  return NULL;
}
