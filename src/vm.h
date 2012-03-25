/**
 * @file vm.h
 * @brief Headers for the virtual-machine and generally lua function-related
 *        structs and such
 */

#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

#include "llvm.h"
#include "lstate.h"
#include "lstring.h"
#include "luav.h"
#include "trace.h"

#define VM_STACK_INIT 1024

struct lhash;

typedef u32 cfunction_t(LSTATE);

/* Instructions packaged with extra tracing information */
typedef struct instr {
  u32       instr;  //<! The lua opcode for this instruction
  u32       count;  //<! Number of times the instruction has been run
  jfunc_t   *jfunc; //<! Compiled code starting from this instruction
} instr_t;

/* Package of a parsed function, and lots of metadata about it */
typedef struct lfunc {
  lstring_t   *name;            //<! Name (in theory, doesn't work with luac?)
  char        *file;            //<! File name this function came from
  u32         start_line;       //<! Source line which was the start of the func
  u32         end_line;         //<! Source line which was the end of the func
  u8          num_upvalues;     //<! Number of upvalues
  u8          num_parameters;   //<! Number of parametrs
  u8          is_vararg;        //<! Parsed from the function...
  u8          max_stack;        //<! Maximum stack space needed

  size_t      num_instrs;       //<! Size of the instructions array
  instr_t     *instrs;          //<! Array of instructions
  size_t      num_consts;       //<! Size of the constants array
  luav        *consts;          //<! Constants
  size_t      num_funcs;        //<! Size of the nested functions array
  struct lfunc **funcs;         //<! Pointers to nested functions

  /* debug information */
  u32           num_lines;      //<! Number of debug lines reported
  u32           *lines;         //<! Corresponding line number for each inst
  trace_t       trace;          //<! JIT tracing information
  i32           *preds;         //<! Predecessor array
} lfunc_t;

/* Package for representing a C function */
typedef struct cfunc {
  cfunction_t *f; //<! Function pointer
  char *name;     //<! Human-readable name for the function
  int upvalues;   //<! Occasionally it might actually have some upvalues!
} cfunc_t;

enum lclosure_type { LUAF_C, LUAF_LUA };

/* Package for representing a closure in lua */
typedef struct lclosure {
  enum lclosure_type type;  //<! The type of the closure (lua/c/etc.)
  struct lhash *env;        //<! Lua environment for the closure
  union {
    lfunc_t *lua;
    cfunc_t *c;
  } function;         //<! Contains the actual function pointer
  luav upvalues[1];   //<! Actual upvalues
} lclosure_t;

/* Metadata for a stack frame of a lua invocation */
typedef struct lframe {
  lclosure_t *closure;    //<! What is being run
  instr_t **pc;           //<! How far the program has gotten
  struct lframe *caller;  //<! Parent frame
} lframe_t;

/* Implementation of lua stacks */
typedef struct lstack {
  luav *top;  //<! Top of the stack's limit
  luav *base; //<! Base of the stack
  u32 size;   //<! Current size of the stack
  u32 limit;  //<! Limit of the size of the stack
} lstack_t;

/**
 * @brief Calculate the amount of memory needed for a closure
 *
 * When allocating a closure, the upvalues are placed directly into the
 * allocation, avoiding the need for another allocation for an upvalues array.
 * For this reason, the allocation of a closure has a variable size, and the
 * number of upvalues needs to be taken into account.
 *
 * @param upvalues the number of upvalues for the closure to be allocated
 * @return the size of the closure to allocate
 */
#define CLOSURE_SIZE(upvalues) (sizeof(lclosure_t) + (upvalues) * sizeof(luav))

extern struct lhash *lua_globals;
extern lframe_t *vm_running;
extern lstack_t *vm_stack;
extern struct lhash *global_env;

lclosure_t* cfunc_alloc(cfunction_t *f, char *name, int upvalues);
void cfunc_register(struct lhash *table, char *name, cfunction_t *f);

void vm_run(lfunc_t *fun);
u32 vm_fun(lclosure_t *c, lframe_t *frame, LSTATE);
void vm_stack_init(lstack_t *stack, u32 size);
void vm_stack_destroy(lstack_t *stack);

void vm_stack_grow(lstack_t *stack, u32 size);
u32 vm_stack_alloc(lstack_t *stack, u32 size);
void vm_stack_dealloc(lstack_t *stack, u32 base);

#endif /* _VM_H_ */
