/**
 * @file llvm.c
 * @brief Will eventually contain JIT-related compliation tied into LLVM
 */

#include <assert.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Transforms/Scalar.h>
#include <stddef.h>
#include <string.h>

#include "config.h"
#include "lhash.h"
#include "llvm.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

typedef LLVMValueRef      Value;
typedef LLVMTypeRef       Type;
typedef LLVMBasicBlockRef BasicBlock;
typedef LLVMValueRef(Binop)(LLVMBuilderRef, Value, Value, const char*);
typedef i32(jitf)(void*, void*);

#define GOTOBB(idx) LLVMBuildBr(builder, DSTBB(idx))
#define DSTBB(idx) (blocks[idx] == NULL ? BAILBB(idx) : blocks[idx])
#define BAILBB(idx) NEWBB(idx, bail_blocks, {})
#define ERRBB(idx)  NEWBB(idx, err_blocks, { ERROR(); })
#define NEWBB(idx, arr, extra) ({                                   \
    BasicBlock tmp = arr[idx];                                      \
    if (tmp == NULL) {                                              \
      BasicBlock cur = LLVMGetInsertBlock(builder);                 \
      tmp = LLVMInsertBasicBlock(ret_block, "");                    \
      LLVMPositionBuilderAtEnd(builder, tmp);                       \
      { extra };                                                    \
      RETURN((i32) (idx));                                          \
      LLVMPositionBuilderAtEnd(builder, cur);                       \
      arr[idx] = tmp;                                               \
    }                                                               \
    tmp;                                                            \
  })
#define RETURN(ret)                                                   \
  Value r = LLVMConstInt(llvm_i32, (long long unsigned) (ret), TRUE); \
  LLVMBuildStore(builder, r, ret_val);                                \
  LLVMBuildBr(builder, ret_block);
#define ERROR() {                                                              \
    Value ptr = LLVMConstInt(llvm_u64, (size_t) &jit_bailed, FALSE);           \
    LLVMBuildStore(builder, lvc_32_one, LLVMConstIntToPtr(ptr, llvm_u32_ptr)); \
  }
#define STOP_ON(cond, ...) {                  \
    if (cond) {                               \
      if (func->instrs[i - 1].count == 0) {   \
        LLVMBuildBr(builder, ERRBB(i - 1));   \
        break;                                \
      }                                       \
      warn(__VA_ARGS__);                      \
      EXIT_FAIL;                              \
    }                                         \
  }
#define TYPE(idx) \
  ((u8) (((idx) >= 256 ? \
          (lv_gettype(func->consts[(idx) - 256]) | TRACE_CONST):\
          regtyps[idx]) & ~TRACE_UPVAL))
#define LTYPE(idx) ((u8) (TYPE(idx) & TRACE_TYPEMASK))
#define SETTYPE(idx, typ) \
  regtyps[idx] = (u8) ((regtyps[idx] & TRACE_UPVAL) | (typ))
#define TOPTR(v) ({                                             \
    Value __tmp = LLVMBuildAnd(builder, v, lvc_data_mask, "");  \
    LLVMBuildIntToPtr(builder, __tmp, llvm_void_ptr, "");       \
  })
#define warn(fmt, ...) fprintf(stderr, "[pc:%d, line:%d, cnt:%d](%d => %d) " fmt "\n", \
                               i - 1, func->lines[i - 1], \
                               func->instrs[i-1].count,start, end, ## __VA_ARGS__)
#define ADD_FUNCTION2(name, str, ret, numa, ...)                  \
  Type name##_args[numa] = {__VA_ARGS__};                         \
  Type name##_type = LLVMFunctionType(ret, name##_args, numa, 0); \
  llvm_functions[llvm_fn_cnt++] = LLVMAddFunction(module, str, name##_type)
#define ADD_FUNCTION(name, ret, numa, ...) \
  ADD_FUNCTION2(name, #name, ret, numa, __VA_ARGS__)
#define EXIT_FAIL LLVMDeleteFunction(function); return -1

typedef struct prolog {
  Value*  stacki;
  Value*  retc;
  Value*  retvi;
  Value*  argc;
  Value*  argca;
  Value*  argvi;
  Value*  argvia;
  Value*  parent;
} prolog_t;

typedef struct state {
  Value       *regs;
  Value       *consts;
  u8          *types;
  lfunc_t     *func;
  Value       function;
  BasicBlock  *blocks;
} state_t;

/* Global contexts for LLVM compilation */
static LLVMModuleRef module;
static LLVMPassManagerRef pass_manager;
static LLVMExecutionEngineRef ex_engine;
static LLVMBuilderRef builder;

static Type llvm_i32;
static Type llvm_u32;
static Type llvm_u64;
static Type llvm_u32_ptr;
static Type llvm_u64_ptr;
static Type llvm_double;
static Type llvm_double_ptr;
static Type llvm_void_ptr;
static Type llvm_void_ptr_ptr;

static Value lvc_null;
static Value lvc_32_zero;
static Value lvc_32_one;
static Value lvc_32_two;
static Value lvc_data_mask;
static Value lvc_type_mask;
static Value lvc_nan_mask;
static Value lvc_nil;
static Value lvc_false;
static Value lvc_luav;

/* Functions used */
static Value llvm_lhash_get;
static Value llvm_lhash_set;
static Value llvm_vm_fun;
static Value llvm_memcpy;
static Value llvm_memmove;
static Value llvm_gc_check;
static Value llvm_vm_alloc;
static Value llvm_functions[128];
static u32   llvm_fn_cnt = 0;

Value build_pow(LLVMBuilderRef builder, Value bv, Value cv, const char* name);
static Value get_stack_base(Value base_addr, Value offset, char *name);
static Value get_vm_stack_base(void);

/**
 * @brief Initialize LLVM globals and engines needed for JIT compilation
 */
void llvm_init() {
  LLVMLinkInJIT();
  LLVMInitializeNativeTarget();
  module = LLVMModuleCreateWithName("joule");
  xassert(module != NULL);

  /* Optimization passes */
  pass_manager = LLVMCreateFunctionPassManagerForModule(module);
  xassert(pass_manager != NULL);
  LLVMAddVerifierPass(pass_manager);
  LLVMAddCFGSimplificationPass(pass_manager);
  LLVMAddJumpThreadingPass(pass_manager);
  LLVMAddPromoteMemoryToRegisterPass(pass_manager);
  LLVMAddReassociatePass(pass_manager);
  LLVMAddGVNPass(pass_manager);
  LLVMAddConstantPropagationPass(pass_manager);
  LLVMAddDeadStoreEliminationPass(pass_manager);
  LLVMAddAggressiveDCEPass(pass_manager);
  LLVMAddIndVarSimplifyPass(pass_manager);
  LLVMAddLoopRotatePass(pass_manager);
  LLVMAddLICMPass(pass_manager);
  LLVMAddLoopUnrollPass(pass_manager);
  LLVMAddLoopUnswitchPass(pass_manager);
  LLVMAddSCCPPass(pass_manager);
  LLVMAddInstructionCombiningPass(pass_manager);
  LLVMAddMemCpyOptPass(pass_manager);
  LLVMAddTailCallEliminationPass(pass_manager);
  LLVMInitializeFunctionPassManager(pass_manager);

  /* Builder and execution engine */
  char *errs;
  LLVMBool err = LLVMCreateJITCompilerForModule(&ex_engine, module, 100, &errs);
  xassert(!err);
  builder = LLVMCreateBuilder();
  xassert(builder != NULL);

  /* Useful types used in lots of places */
  llvm_i32          = LLVMInt32Type();
  llvm_u32          = llvm_i32;
  llvm_u64          = LLVMInt64Type();
  llvm_u32_ptr      = LLVMPointerType(llvm_u32, 0);
  llvm_u64_ptr      = LLVMPointerType(llvm_u64, 0);
  llvm_double       = LLVMDoubleType();
  llvm_double_ptr   = LLVMPointerType(llvm_double, 0);
  llvm_void_ptr     = LLVMPointerType(LLVMInt8Type(), 0);
  llvm_void_ptr_ptr = LLVMPointerType(llvm_void_ptr, 0);

  /* Constants */
  lvc_null      = LLVMConstNull(llvm_void_ptr);
  lvc_32_zero   = LLVMConstInt(llvm_u32, 0, FALSE);
  lvc_32_one    = LLVMConstInt(llvm_u32, 1, FALSE);
  lvc_32_two    = LLVMConstInt(llvm_u32, 2, FALSE);
  lvc_data_mask = LLVMConstInt(llvm_u64, LUAV_DATA_MASK, FALSE);
  lvc_type_mask = LLVMConstInt(llvm_u64, LUAV_TYPE_MASK, FALSE);
  lvc_nan_mask  = LLVMConstInt(llvm_u64, LUAV_NAN_MASK, FALSE);
  lvc_nil       = LLVMConstInt(llvm_u64, LUAV_NIL, FALSE);
  lvc_false     = LLVMConstInt(llvm_u64, LUAV_FALSE, FALSE);
  lvc_luav      = LLVMConstInt(llvm_u32, sizeof(luav), FALSE);

  /* Adding functions */
  ADD_FUNCTION(lhash_get, llvm_u64, 2, llvm_void_ptr, llvm_u64);
  ADD_FUNCTION(lhash_set, LLVMVoidType(), 3, llvm_void_ptr, llvm_u64, llvm_u64);
  ADD_FUNCTION(vm_fun, llvm_u32, 6, llvm_void_ptr, llvm_void_ptr, llvm_u32,
                                    llvm_u32, llvm_u32, llvm_u32);
  ADD_FUNCTION2(llvm_memset, "llvm.memset.p0i8.i32", LLVMVoidType(), 5,
                llvm_void_ptr, LLVMInt8Type(), llvm_u32, llvm_u32,
                LLVMInt1Type());
  ADD_FUNCTION2(llvm_pow, "llvm.pow.f64", llvm_double, 2, llvm_double,
                llvm_double);
  ADD_FUNCTION(lhash_hint, llvm_void_ptr, 2, llvm_u32, llvm_u32);
  ADD_FUNCTION(lstr_compare, llvm_i32, 2, llvm_void_ptr, llvm_void_ptr);
  ADD_FUNCTION(lclosure_alloc, llvm_void_ptr, 2, llvm_void_ptr, llvm_u32);
  ADD_FUNCTION(lupvalue_alloc, llvm_u64, 1, llvm_u64);
  ADD_FUNCTION2(llvm_memmove, "llvm.memmove.p0i8.p0i8.i32", LLVMVoidType(), 5,
                llvm_void_ptr, llvm_void_ptr, llvm_u32, llvm_u32,
                LLVMInt1Type());
  ADD_FUNCTION2(llvm_memcpy, "llvm.memcpy.p0i8.p0i8.i32", LLVMVoidType(), 5,
                llvm_void_ptr, llvm_void_ptr, llvm_u32, llvm_u32,
                LLVMInt1Type());
  ADD_FUNCTION(lv_concat, llvm_u64, 2, llvm_u64, llvm_u64);
  ADD_FUNCTION(lhash_array, LLVMVoidType(), 3, llvm_void_ptr, llvm_u64_ptr,
               llvm_u32);
  ADD_FUNCTION(gc_check, LLVMVoidType(), 0);
  ADD_FUNCTION(vm_funi, llvm_u32, 9, llvm_void_ptr, llvm_void_ptr, llvm_u32,
               llvm_u32, llvm_u32, llvm_u32, llvm_u32, llvm_u32, llvm_u32);
  ADD_FUNCTION(vm_stack_alloc, llvm_u32, 2, llvm_void_ptr, llvm_u32);

  llvm_lhash_get = LLVMGetNamedFunction(module, "lhash_get");
  llvm_lhash_set = LLVMGetNamedFunction(module, "lhash_set");
  llvm_vm_fun    = LLVMGetNamedFunction(module, "vm_fun");
  llvm_memcpy    = LLVMGetNamedFunction(module, "llvm.memcpy.p0i8.p0i8.i32");
  llvm_memmove   = LLVMGetNamedFunction(module, "llvm.memmove.p0i8.p0i8.i32");
  llvm_gc_check  = LLVMGetNamedFunction(module, "gc_check");
  llvm_vm_alloc  = LLVMGetNamedFunction(module, "vm_stack_alloc");

  /* Build our trampoline function for when error happens in a fully compiled
     function */
  Type args[4] = {
    llvm_void_ptr, /* closure */
    llvm_void_ptr, /* frame */
    llvm_u32,      /* instruction */
    llvm_u32       /* stacki */
  };
  Type funtyp = LLVMFunctionType(llvm_u64, args, 4, FALSE);
  Value func = LLVMAddFunction(module, "trampoline", funtyp);
  LLVMAddFunctionAttr(func, LLVMNoInlineAttribute);
  BasicBlock b = LLVMAppendBasicBlock(func, "");
  LLVMPositionBuilderAtEnd(builder, b);

  Value vmargs[9] = {
    LLVMGetParam(func, 0),
    LLVMGetParam(func, 1),
    LLVMGetParam(func, 3),
    lvc_32_zero,
    LLVMGetParam(func, 2),
    lvc_32_zero,
    lvc_32_zero,
    lvc_32_one,
    LLVMGetParam(func, 3)
  };
  Value retc = LLVMBuildCall(builder, LLVMGetNamedFunction(module, "vm_funi"),
                             vmargs, 9, "");
  Value addr = get_stack_base(get_vm_stack_base(), LLVMGetParam(func, 3), "");
  addr       = LLVMBuildInBoundsGEP(builder, addr, &lvc_32_zero, 1, "");
  Value from_stack = LLVMBuildLoad(builder, addr, "");

  Value cond = LLVMBuildICmp(builder, LLVMIntEQ, retc, lvc_32_one, "");
  Value ret = LLVMBuildSelect(builder, cond, from_stack, lvc_nil, "");
  LLVMBuildRet(builder, ret);
}

/**
 * @brief Deallocates all memory associated with LLVM allocated on startup
 */
void llvm_destroy() {
  u32 i;
  for (i = 0; i < llvm_fn_cnt; i++) {
    LLVMDeleteFunction(llvm_functions[i]);
  }

  LLVMFinalizeFunctionPassManager(pass_manager);
  LLVMDisposePassManager(pass_manager);
  LLVMDisposeBuilder(builder);
  LLVMDisposeExecutionEngine(ex_engine);
  LLVMContextDispose(LLVMGetGlobalContext());
}

/**
 * @brief Calculates a stack pointer which is relative to the base of the lua
 *        stack
 *
 * @param base_addr the address which when dereferenced will point to the base
 *        of the lua stack
 * @param offset the offset into the lua stack to calculate as an address
 * @param name the name of the variable to make
 */
static Value get_stack_base(Value base_addr, Value offset, char *name) {
  Value stack = LLVMBuildLoad(builder, base_addr, "");
  return LLVMBuildInBoundsGEP(builder, stack, &offset, 1, name);
}

/**
 * @brief Calculates the address which contains the base of the stack_typ
 */
static Value get_vm_stack_base(void) {
  Value vm_stack_ptr = LLVMConstInt(llvm_u64, (size_t) &vm_stack, FALSE);
  vm_stack_ptr       = LLVMConstIntToPtr(vm_stack_ptr, llvm_void_ptr_ptr);
  Value base_addr    = LLVMBuildLoad(builder, vm_stack_ptr, "");
  Value base_offset  = LLVMConstInt(llvm_u32, offsetof(lstack_t, base), FALSE);
  base_addr          = LLVMBuildInBoundsGEP(builder, base_addr, &base_offset, 1,
                                            "");
  Type stack_typ = LLVMPointerType(LLVMPointerType(llvm_u64, 0), 0);
  return LLVMBuildBitCast(builder, base_addr, stack_typ, "");
}

/**
 * @brief Builds a register set
 *
 * @param s the current state
 * @param idx the index of the register to set
 * @param v the value to set the register to as a u64
 */
static void build_regset(state_t *s, u32 idx, Value v) {
  Value addr = s->regs[idx];
  if (TRACE_ISUPVAL(s->types[idx])) {
    addr = TOPTR(LLVMBuildLoad(builder, addr, ""));
    addr = LLVMBuildBitCast(builder, addr, llvm_u64_ptr, "");
  }
  LLVMBuildStore(builder, v, addr);
}

/**
 * @brief Builds a register access
 *
 * @param s the current state
 * @param idx the register index
 *
 * @return the register as a u64
 */
static Value build_reg(state_t *s, u32 idx) {
  Value tmp = LLVMBuildLoad(builder, s->regs[idx], "");
  if (TRACE_ISUPVAL(s->types[idx])) {
    tmp = LLVMBuildBitCast(builder, TOPTR(tmp), llvm_u64_ptr, "");
    tmp = LLVMBuildLoad(builder, tmp, "");
  }
  return tmp;
}

/**
 * @brief Build a register access, which could actually be a constant access
 *
 * @param s the current state
 * @param reg the number of the register to access
 *
 * @return the corresponding register value, as a u64
 */
static Value build_kregu(state_t *s, u32 reg) {
  if (reg >= 256) {
    return s->consts[reg - 256];
  } else {
    return build_reg(s, reg);
  }
}

/**
 * @brief Build a register access, which could actually be a constant access
 *
 * @param s the current state
 * @param reg the number of the register to access
 *
 * @return the corresponding register value, as a double
 */
static Value build_kregf(state_t *s, u32 reg) {
  return LLVMBuildBitCast(builder, build_kregu(s, reg), llvm_double, "");
}

/**
 * @brief Build a binary operation of two values for a function
 *
 * Builds the LLVM instructions necessary to load the operands, perform the
 * operation, and then store the result.
 *
 * @param s the current state
 * @param code the lua opcode
 * @param operation the LLVM binary operation to perform
 */
static void build_binop(state_t *s, u32 code, Binop operation) {
  Value bv = build_kregf(s, B(code));
  Value cv = build_kregf(s, C(code));
  Value res = operation(builder, bv, cv, "");
  res = LLVMBuildBitCast(builder, res, llvm_u64, "");
  build_regset(s, A(code), res);
}

/**
 * @brief Get the base of the variable number of arguments on the stack, based
 *        on the previous instruction
 *
 * @param s the current state
 * @param pc the program counter which needs the variable arguments
 * @return the base of where the variable arguments are located
 */
static i32 get_varbase(state_t *s, u32 pc) {
  if ((i32) pc - 2 < 0) { return -1; }
  switch (OP(s->func->instrs[pc - 2].instr)) {
    case OP_CALL:
      if (C(s->func->instrs[pc - 2].instr) != 0) return -1;
      return A(s->func->instrs[pc - 2].instr);

    case OP_VARARG:
      if (B(s->func->instrs[pc - 2].instr) != 0) return -1;
      return A(s->func->instrs[pc - 2].instr);

    case OP_TAILCALL:
      return 0;
  }
  return -1;
}

/**
 * @brief Build a fetch of the version number of a table, returned as a
 *        u64
 *
 * @param table the void* pointer to the table
 * @return the u64 version number
 */
static Value build_lhash_version(Value table) {
  Value offset = LLVMConstInt(llvm_u32, offsetof(lhash_t, version), FALSE);
  Value addr = LLVMBuildInBoundsGEP(builder, table, &offset, 1, "");
  addr = LLVMBuildBitCast(builder, addr, llvm_u64_ptr, "");
  return LLVMBuildLoad(builder, addr, "");
}

/**
 * @brief Performs a table lookup with cached stuff
 *
 *
 * @return
 */
static void build_lhash_get(state_t *state, size_t i, Value table, Value key,
                            int is_const, u32 index) {
  /* TODO: metatable? */
  if (is_const && state->blocks[i + 1] != NULL) {
    BasicBlock equal, diff;
    equal = LLVMAppendBasicBlock(state->function, "");
    diff = LLVMAppendBasicBlock(state->function, "");
    Value version = build_lhash_version(table);
    u64 *trace_version    = &state->func->trace.misc[i].table.version;
    luav *trace_value     = &state->func->trace.misc[i].table.value;
    lhash_t **trace_table = &state->func->trace.misc[i].table.pointer;
    Value tversion = LLVMConstInt(llvm_u64, (size_t) trace_version, FALSE);
    Value tvalue = LLVMConstInt(llvm_u64, (size_t) trace_value, FALSE);
    Value ttable = LLVMConstInt(llvm_u64, (size_t) trace_table, FALSE);
    tversion = LLVMConstIntToPtr(tversion, llvm_u64_ptr);
    tvalue = LLVMConstIntToPtr(tvalue, llvm_u64_ptr);
    ttable = LLVMConstIntToPtr(ttable, llvm_void_ptr_ptr);
    Value exp_table   = LLVMBuildLoad(builder, ttable, "");
    Value exp_version = LLVMBuildLoad(builder, tversion, "");

    Value eq_tab = LLVMBuildICmp(builder, LLVMIntEQ, table, exp_table, "");
    Value eq_ver = LLVMBuildICmp(builder, LLVMIntEQ, version, exp_version, "");
    Value eq = LLVMBuildAnd(builder, eq_tab, eq_ver, "");
    LLVMBuildCondBr(builder, eq, equal, diff);

    /* If the version number was the same, used the traced value */
    LLVMPositionBuilderAtEnd(builder, equal);
    build_regset(state, index, LLVMBuildLoad(builder, tvalue, ""));
    LLVMBuildBr(builder, state->blocks[i + 1]);

    /* If the version number was different, do the get and update the
       traced information */
    LLVMPositionBuilderAtEnd(builder, diff);
    Value args[2] = {table, key};
    Value val = LLVMBuildCall(builder, llvm_lhash_get, args, 2, "");
    build_regset(state, index, val);
    LLVMBuildStore(builder, table, ttable);
    LLVMBuildStore(builder, build_lhash_version(table), tversion);
    LLVMBuildStore(builder, val, tvalue);
  } else {
    // key isn't constant
    Value args[2] = {table, key};
    Value val = LLVMBuildCall(builder, llvm_lhash_get, args, 2, "");
    build_regset(state, index, val);
  }
}

/**
 * @brief Builds the prolog for a partially compiled function segment
 */
static void build_partial_prolog(state_t *state, prolog_t *pro,
                                 Value last_ret_addr, Value last_ret) {
  /* Calculate stacki, and LSTATE */
  Value jargs   = LLVMGetParam(state->function, 1);
  Value stackio = LLVMConstInt(llvm_u32, JSTACKI, FALSE);
  Value stackia = LLVMBuildInBoundsGEP(builder, jargs, &stackio, 1, "");
  *pro->stacki  = LLVMBuildLoad(builder, stackia, "stacki");
  Value retco   = LLVMConstInt(llvm_u32, JRETC, FALSE);
  Value retca   = LLVMBuildInBoundsGEP(builder, jargs, &retco, 1, "");
  *pro->retc    = LLVMBuildLoad(builder, retca, "retc");
  Value retvio  = LLVMConstInt(llvm_u32, JRETVI, FALSE);
  Value retvia  = LLVMBuildInBoundsGEP(builder, jargs, &retvio, 1, "");
  *pro->retvi   = LLVMBuildLoad(builder, retvia, "retvi");
  Value argco   = LLVMConstInt(llvm_u32, JARGC, FALSE);
  *pro->argca   = LLVMBuildInBoundsGEP(builder, jargs, &argco, 1, "");
  *pro->argc    = LLVMBuildLoad(builder, *pro->argca, "argc");
  Value argvio  = LLVMConstInt(llvm_u32, JARGVI, FALSE);
  *pro->argvia  = LLVMBuildInBoundsGEP(builder, jargs, &argvio, 1, "");
  *pro->argvi   = LLVMBuildLoad(builder, *pro->argvia, "argvi");

  /* Load last_ret */
  LLVMBuildStore(builder, LLVMBuildLoad(builder, last_ret_addr, ""), last_ret);

  /* Calculate the currently running frame (parent of all other invocations) */
  Value parent = LLVMConstInt(llvm_u64, (size_t) &vm_running, FALSE);
  parent       = LLVMConstIntToPtr(parent, llvm_void_ptr_ptr);
  *pro->parent  = LLVMBuildLoad(builder, parent, "parent");

  /* Load address of vm_stack->base points to */
  Value base_addr = get_vm_stack_base();

  /* Copy the lua stack onto the C stack */
  u32 i;
  Value lstack = get_stack_base(base_addr, *pro->stacki, "lstack");
  for (i = 0; i < state->func->max_stack; i++) {
    Value off  = LLVMConstInt(llvm_u32, i, 0);
    Value addr = LLVMBuildInBoundsGEP(builder, lstack, &off, 1, "");
    Value val  = LLVMBuildLoad(builder, addr, "");
    LLVMBuildStore(builder, val, state->regs[i]);
  }

}

/**
 * @brief Build a load at runtime of the specified C pointer
 *
 * @param ptr the C pointer to load
 * @param addr optionally also store the address of the pointer in an llvm value
 * @return the llvm value which is the load of the given addres, typed as a void
 *         pointer.
 */
static Value build_dynload(void *ptr, Value *addr) {
  Value llvmaddr = LLVMConstInt(llvm_u64, (size_t) ptr, FALSE);
  llvmaddr = LLVMConstIntToPtr(llvmaddr, llvm_void_ptr_ptr);
  if (addr != NULL) { *addr = llvmaddr; }
  return LLVMBuildLoad(builder, llvmaddr, "");
}

static void build_full_prolog(state_t *s, prolog_t *p) {
  u32 i;
  Type targs[s->func->num_parameters + 1];
  targs[0] = llvm_void_ptr;
  for (i = 0; i < s->func->max_stack; i++) {
    targs[i + 1] = llvm_u64;
  }
  Type funtyp = LLVMFunctionType(llvm_u64, targs, s->func->num_parameters + 1,
                                 FALSE);
  s->function = LLVMAddFunction(module, "compiled", funtyp);
  lfunc_t *func = s->func;
  /* These all don't matter in full function compilation */
  *p->argc = *p->argvi = *p->retc = *p->retvi = *p->argca = *p->argvia =
    lvc_32_zero;

  /* Place all arguments into their registers */
  for (i = 0; i < func->num_parameters; i++) {
    LLVMBuildStore(builder, s->regs[i], LLVMGetParam(s->function, i + 1));
  }
  /* Nil-ify all other registers */
  for (; i < func->max_stack; i++) {
    LLVMBuildStore(builder, s->regs[i], lvc_nil);
  }
  Value closure = LLVMGetParam(s->function, 0);
  /* Figure out the current parent */
  Value parent_addr, parent;
  parent = build_dynload(&vm_running, &parent_addr);

  /* Allocate a frame on the stack, and fill it in */
  Type typs[3] = {llvm_void_ptr, llvm_void_ptr, llvm_void_ptr};
  Type lframe_typ = LLVMStructType(typs, 3, FALSE);
  Value frame = LLVMBuildAlloca(builder, lframe_typ, "frame");
  LLVMBuildStore(builder, closure, LLVMBuildStructGEP(builder, frame, 0, ""));
  LLVMBuildStore(builder, lvc_null, LLVMBuildStructGEP(builder, frame, 1, ""));
  LLVMBuildStore(builder, parent, LLVMBuildStructGEP(builder, frame, 2, ""));
  /* Set ourselves as the currently running frame */
  LLVMBuildStore(builder, frame, parent_addr);
  *p->parent = frame;

  /* Allocate some lua stack */
  Value args[2] = {
    build_dynload(&vm_stack, NULL),
    LLVMConstInt(llvm_u32, func->max_stack, FALSE)
  };
  *p->stacki = LLVMBuildCall(builder, llvm_vm_alloc, args, 2, "stacki");
}

/**
 * @brief JIT-Compile a function
 *
 * @param func the function to compile
 * @param start the beginning program counter to compile at
 * @param end the ending program counter to compile. The opcode at this
 *        counter will be compiled.
 * @param stack the current stack of the lua function
 *
 * @return the compiled function, which can be run by passing it over
 *         to llvm_run()
 */
i32 llvm_compile(struct lfunc *func, u32 start, u32 end,
                 luav *stack, jfunc_t *jfun) {
  jfun->value = NULL;
  jfun->binary = NULL;

  BasicBlock blocks[func->num_instrs];
  BasicBlock bail_blocks[func->num_instrs];
  BasicBlock err_blocks[func->num_instrs];
  Value regs[func->max_stack];
  Value consts[func->num_consts];
  u8    regtyps[func->max_stack];
  char name[20];
  u32 i, j;

  /* Create the function and state */
  Type params[2] = {llvm_void_ptr, LLVMPointerType(llvm_u32, 0)};
  Type funtyp    = LLVMFunctionType(llvm_u32, params, 2, FALSE);
  Value function = LLVMAddFunction(module, "test", funtyp);
  Value closure = LLVMGetParam(function, 0);
  state_t s = {
    .regs     = regs,
    .consts   = consts,
    .types    = regtyps,
    .func     = func,
    .function = function,
    .blocks   = blocks
  };

  BasicBlock startbb = LLVMAppendBasicBlock(function, "start");
  /* Create the blocks and allocas */
  memset(blocks, 0, sizeof(blocks));
  memset(bail_blocks, 0, sizeof(bail_blocks));
  memset(err_blocks, 0, sizeof(err_blocks));
  for (i = start; i <= end; i++) {
    if (func->instrs[i].count == 0) { continue; }
    sprintf(name, "block%d", i);
    blocks[i] = LLVMAppendBasicBlock(function, name);
  }
  LLVMPositionBuilderAtEnd(builder, startbb);
  LLVMBuildCall(builder, llvm_gc_check, NULL, 0, "");
  for (i = 0; i < func->num_consts; i++) {
    consts[i] = LLVMConstInt(llvm_u64, func->consts[i], FALSE);
  }
  for (i = 0; i < func->max_stack; i++) {
    regs[i] = LLVMBuildAlloca(builder, llvm_u64, "");
  }
  Value offset = LLVMConstInt(llvm_u64, offsetof(lclosure_t, last_ret), 0);
  Value ret_val  = LLVMBuildAlloca(builder, llvm_i32, "ret_val");
  Value last_ret = LLVMBuildAlloca(builder, llvm_i32, "last_ret");
  Value last_ret_addr = LLVMBuildInBoundsGEP(builder, closure, &offset, 1,"");
  last_ret_addr = LLVMBuildBitCast(builder, last_ret_addr, llvm_u32_ptr, "");

  /* Create the function prolog */
  Value stacki, retc, retvi, argc, argca, argvi, argvia, parent;
  prolog_t pro = {
    .stacki   = &stacki,
    .retc     = &retc,
    .retvi    = &retvi,
    .argc     = &argc,
    .argca    = &argca,
    .argvi    = &argvi,
    .argvia   = &argvia,
    .parent   = &parent
  };
  build_partial_prolog(&s, &pro, last_ret_addr, last_ret);

  /* Calculate closure->env */
  offset = LLVMConstInt(llvm_u64, offsetof(lclosure_t, env), 0);
  Value env_addr = LLVMBuildInBoundsGEP(builder, closure, &offset, 1,"");
  env_addr = LLVMBuildBitCast(builder, env_addr, llvm_void_ptr_ptr, "");
  Value closure_env = LLVMBuildLoad(builder, env_addr, "env");

  /* Calculate &closure->upvalues, and jump to instruction 'start' */
  Value upv_off  = LLVMConstInt(llvm_u64, offsetof(lclosure_t, upvalues), 0);
  Value upv_addr = LLVMBuildInBoundsGEP(builder, closure, &upv_off, 1,"");
  Value upvalues = LLVMBuildBitCast(builder, upv_addr, llvm_u64_ptr, "");
  Value base_addr = get_vm_stack_base();
  LLVMBuildBr(builder, blocks[start]);

  /* Create exit block */
  BasicBlock ret_block = LLVMAppendBasicBlock(function, "exit");
  LLVMPositionBuilderAtEnd(builder, ret_block);
  Value stack_ptr = get_stack_base(base_addr, stacki, "stack");
  for (i = 0; i < func->max_stack; i++) {
    Value off  = LLVMConstInt(llvm_u32, i, FALSE);
    Value addr = LLVMBuildInBoundsGEP(builder, stack_ptr, &off, 1, "");
    Value val  = LLVMBuildLoad(builder, regs[i], "");
    LLVMBuildStore(builder, val, addr);
  }
  LLVMBuildStore(builder, LLVMBuildLoad(builder, last_ret, ""), last_ret_addr);
  Value r = LLVMBuildLoad(builder, ret_val, "");
  LLVMBuildRet(builder, r);

  /* Initialize the types of all stack members */
  for (i = 0; i < func->max_stack; i++) {
    if (lv_isupvalue(stack[i])) {
      regtyps[i] = TRACE_UPVAL | lv_gettype(*lv_getupvalue(stack[i]));
    } else {
      regtyps[i] = lv_gettype(stack[i]);
    }
  }

  /* Translate! */
  for (i = start; i <= end;) {
    if (blocks[i] == NULL) { i++; continue; }
    LLVMPositionBuilderAtEnd(builder, blocks[i]);
    u32 code = func->instrs[i++].instr;

    switch (OP(code)) {
      case OP_MOVE: {
        build_regset(&s, A(code), build_reg(&s, B(code)));
        SETTYPE(A(code), TYPE(B(code)));
        GOTOBB(i);
        break;
      }

      case OP_LOADK: {
        build_regset(&s, A(code), consts[BX(code)]);
        SETTYPE(A(code), lv_gettype(func->consts[BX(code)]) | TRACE_CONST);
        GOTOBB(i);
        break;
      }

      case OP_LOADNIL: {
        for (j = A(code); j <= B(code); j++) {
          build_regset(&s, j, lvc_nil);
          SETTYPE(j, LNIL | TRACE_CONST);
        }
        GOTOBB(i);
        break;
      }

      case OP_LOADBOOL: {
        Value bv = LLVMConstInt(llvm_u64, B(code) ? LUAV_TRUE : LUAV_FALSE, 0);
        build_regset(&s, A(code), bv);
        SETTYPE(A(code), LBOOLEAN | TRACE_CONST);
        u32 next = C(code) ? i + 1 : i;
        GOTOBB(next);
        break;
      }

      case OP_NOT: {
        STOP_ON(LTYPE(B(code)) != LBOOLEAN, "bad NOT");
        Value bv = build_reg(&s, B(code));
        Value one = LLVMConstInt(llvm_u64, 1, FALSE);
        bv = LLVMBuildXor(builder, bv, one, "");
        build_regset(&s, A(code), bv);
        SETTYPE(A(code), LBOOLEAN);
        GOTOBB(i);
        break;
      }

      /* TODO: assumes floats */
      #define BINIMPL(f)                                                      \
        STOP_ON(LTYPE(B(code)) != LNUMBER || LTYPE(C(code)) != LNUMBER,         \
                "bad arith: %d,%d", LTYPE(B(code)), LTYPE(C(code)));            \
        build_binop(&s, code, f);                                             \
        SETTYPE(A(code), LNUMBER);                                            \
        GOTOBB(i);
      case OP_ADD: BINIMPL(LLVMBuildFAdd)   break;
      case OP_SUB: BINIMPL(LLVMBuildFSub);  break;
      case OP_MUL: BINIMPL(LLVMBuildFMul);  break;
      case OP_DIV: BINIMPL(LLVMBuildFDiv);  break;
      case OP_MOD: BINIMPL(LLVMBuildFRem);  break;
      case OP_POW: BINIMPL(build_pow);      break;

      case OP_UNM: {
        STOP_ON(LTYPE(B(code)) != LNUMBER, "bad UNM");
        Value bv = build_reg(&s, B(code));
        bv = LLVMBuildBitCast(builder, bv, llvm_double, "");
        Value res = LLVMBuildFNeg(builder, bv, "");
        res = LLVMBuildBitCast(builder, res, llvm_u64, "");
        build_regset(&s, A(code), res);
        SETTYPE(A(code), LNUMBER);
        GOTOBB(i);
        break;
      }

      case OP_EQ: {
        u8 btyp = LTYPE(B(code));
        u8 ctyp = LTYPE(C(code));
        Value cond;
        if (btyp == LNUMBER && ctyp == LNUMBER) {
          Value bv  = build_kregf(&s, B(code));
          Value cv  = build_kregf(&s, C(code));
          cond      = LLVMBuildFCmp(builder,
                                     A(code) ? LLVMRealUNE : LLVMRealUEQ,
                                     bv, cv, "");
        } else if (btyp != LANY && ctyp != LANY) {
          Value bv  = build_kregu(&s, B(code));
          Value cv  = build_kregu(&s, C(code));
          cond      = LLVMBuildICmp(builder,
                                     A(code) ? LLVMIntNE : LLVMIntEQ,
                                     bv, cv, "");
        } else {
          STOP_ON(1, "bad EQ (%d, %d)", btyp, ctyp);
        }
        BasicBlock truebb  = DSTBB(i + 1);
        BasicBlock falsebb = DSTBB(i);
        LLVMBuildCondBr(builder, cond, truebb, falsebb);
        break;
      }

      #define CMP_OP(cond1, cond2) {                                          \
          BasicBlock truebb  = DSTBB(i + 1);                                  \
          BasicBlock falsebb = DSTBB(i);                                      \
          if (LTYPE(B(code)) == LNUMBER && LTYPE(C(code)) == LNUMBER) {       \
            Value bv = build_kregf(&s, B(code));                              \
            Value cv = build_kregf(&s, C(code));                              \
            LLVMRealPredicate pred = A(code) ? LLVMRealU##cond1               \
                                             : LLVMRealU##cond2;              \
            Value cond = LLVMBuildFCmp(builder, pred, bv, cv, "");            \
            LLVMBuildCondBr(builder, cond, truebb, falsebb);                  \
          } else if (LTYPE(B(code)) == LSTRING && LTYPE(C(code)) == LSTRING) {\
            Value bv = TOPTR(build_kregu(&s, B(code)));                       \
            Value cv = TOPTR(build_kregu(&s, C(code)));                       \
            LLVMIntPredicate pred = A(code) ? LLVMIntS##cond1                 \
                                            : LLVMIntS##cond2;                \
            Value fn = LLVMGetNamedFunction(module, "lstr_compare");          \
            Value args[2] = {bv, cv};                                         \
            Value r  = LLVMBuildCall(builder, fn, args, 2, "lstr_compare");   \
            Value cond = LLVMBuildICmp(builder, pred, r, lvc_32_zero, "");    \
            LLVMBuildCondBr(builder, cond, truebb, falsebb);                  \
          } else {                                                            \
            /* TODO - metatable */                                            \
            STOP_ON(1, "bad LT/LE (%d, %d)", LTYPE(B(code)), LTYPE(C(code))); \
          }                                                                   \
        }
      case OP_LT: CMP_OP(GE, LT); break;
      case OP_LE: CMP_OP(GT, LE); break;

      case OP_JMP: {
        GOTOBB((u32) ((i32) i + SBX(code)));
        break;
      }

      case OP_FORLOOP: {
        STOP_ON(LTYPE(A(code)) != LNUMBER || LTYPE(A(code) + 1) != LNUMBER ||
                LTYPE(A(code) + 2) != LNUMBER, "bad FORLOOP");
        /* TODO - guard that R(A), R(A+1), R(A+2) are numbers */
        Value a2v = build_kregf(&s, A(code) + 2);
        Value av  = LLVMBuildFAdd(builder, build_kregf(&s, A(code)), a2v, "");
        build_regset(&s, A(code), LLVMBuildBitCast(builder, av, llvm_u64, ""));

        /* TODO - guard that the sign of R(A+2) is the same as in the trace */
        BasicBlock endbb = LLVMAppendBasicBlock(function, "endfor");
        Value a1v  = build_kregf(&s, A(code) + 1);
        LLVMRealPredicate pred = func->trace.instrs[i - 1][0] ? LLVMRealUGE
                                                              : LLVMRealULE;
        Value cond = LLVMBuildFCmp(builder, pred, av, a1v, "");
        LLVMBuildCondBr(builder, cond, endbb, DSTBB(i));

        LLVMPositionBuilderAtEnd(builder, endbb);
        av = LLVMBuildBitCast(builder, av, llvm_u64, "");
        build_regset(&s, A(code) + 3, av);
        GOTOBB((u32) ((i32) i + SBX(code)));
        break;
      }

      case OP_FORPREP: {
        STOP_ON(LTYPE(A(code)) != LNUMBER || LTYPE(A(code) + 2) != LNUMBER,
                "bad FORPREP");
        /* TODO - guard that R(A) and R(A+2) are numbers */
        Value a2v = build_kregf(&s, A(code) + 2);
        Value av  = build_kregf(&s, A(code));
        av = LLVMBuildFSub(builder, av, a2v, "");
        build_regset(&s, A(code), LLVMBuildBitCast(builder, av, llvm_u64, ""));
        SETTYPE(A(code), LNUMBER);
        SETTYPE(A(code) + 1, LNUMBER);
        SETTYPE(A(code) + 2, LNUMBER);
        SETTYPE(A(code) + 3, LNUMBER);
        GOTOBB((u32) ((i32) i + SBX(code)));
        break;
      }

      case OP_RETURN: {
        Value ret_stack = get_stack_base(base_addr, retvi, "retstack");
        if (B(code) == 0) {
          Value stack = get_stack_base(base_addr, stacki, "");
          /* Store remaining registers onto our lua stack */
          i32 end_stores = get_varbase(&s, i);
          if (end_stores < 0) { warn("bad B0 OP_RETURN"); EXIT_FAIL; }
          for (j = A(code); j < (u32) end_stores; j++) {
            Value offset = LLVMConstInt(llvm_u32, j, FALSE);
            Value addr = LLVMBuildInBoundsGEP(builder, stack, &offset, 1, "");
            Value val  = LLVMBuildLoad(builder, regs[j], "");
            LLVMBuildStore(builder, val, addr);
          }
          /* Memcpy our lua stack over to the return stack */
          Value av = LLVMConstInt(llvm_u32, A(code), FALSE);
          Value ret_base = LLVMBuildInBoundsGEP(builder, stack, &av, 1, "");
          Value num_rets = LLVMBuildLoad(builder, last_ret, "");
          num_rets = LLVMBuildSub(builder, num_rets, av, "");
          Value cond = LLVMBuildICmp(builder, LLVMIntULE, num_rets, retc, "");
          Value amt = LLVMBuildSelect(builder, cond, num_rets, retc, "");

          /* memmove(ret_base, ret_stack, min(num_rets, retc) * sizeof(luav)) */
          Value args[5] = {
            LLVMBuildBitCast(builder, ret_stack, llvm_void_ptr, ""),
            LLVMBuildBitCast(builder, ret_base, llvm_void_ptr, ""),
            LLVMBuildMul(builder, amt, lvc_luav, ""),
            LLVMConstInt(llvm_u32, 8, FALSE),
            LLVMConstInt(LLVMInt1Type(), 0, FALSE)
          };
          LLVMBuildCall(builder, llvm_memmove, args, 5, "");
          num_rets = LLVMBuildNeg(builder, num_rets, "");
          num_rets = LLVMBuildSub(builder, num_rets, lvc_32_two, "");
          LLVMBuildRet(builder, num_rets);
          break;
        }

        /* Create actual return first, so everything can jump to it */
        BasicBlock endbb = LLVMAppendBasicBlock(function, "end");
        LLVMPositionBuilderAtEnd(builder, endbb);
        u32 num_ret = B(code) - 1;
        LLVMBuildRet(builder, LLVMConstInt(llvm_u32, (u32)(-num_ret - 2),
                                           TRUE));

        /* Create blocks for all return values, bailing out as soon as possible
           to the end when no more return values are wanted */
        LLVMPositionBuilderAtEnd(builder, blocks[i - 1]);
        for (j = 0; j < num_ret; j++) {
          /* Test whether this argument should be returned */
          Value offset = LLVMConstInt(llvm_u32, j, FALSE);
          BasicBlock curbb = LLVMInsertBasicBlock(endbb, "ret");
          Value cond = LLVMBuildICmp(builder, LLVMIntULT, offset, retc, "");
          LLVMBuildCondBr(builder, cond, curbb, endbb);

          /* Return the argument */
          LLVMPositionBuilderAtEnd(builder, curbb);
          Value addr = LLVMBuildInBoundsGEP(builder, ret_stack, &offset, 1, "");
          Value val  = build_reg(&s, A(code) + j);
          LLVMBuildStore(builder, val, addr);
        }

        LLVMBuildBr(builder, endbb);
        break;
      }

      case OP_GETGLOBAL: {
        build_lhash_get(&s, i - 1, closure_env, consts[BX(code)], 1, A(code));

        /* TODO: guard this */
        SETTYPE(A(code), func->trace.instrs[i - 1][0]);
        GOTOBB(i);
        break;
      }

      case OP_SETGLOBAL: {
        /* TODO: metatable? */
        Value args[3] = {
          closure_env,
          consts[BX(code)],
          build_reg(&s, A(code))
        };
        LLVMBuildCall(builder, llvm_lhash_set, args, 3, "");
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_NEWTABLE: {
        /* Call lhash_hint */
        Value fn = LLVMGetNamedFunction(module, "lhash_hint");
        Value args[2] = {
          LLVMConstInt(llvm_u32, B(code), FALSE),
          LLVMConstInt(llvm_u32, C(code), FALSE)
        };
        Value ret = LLVMBuildCall(builder, fn, args, 2, "");

        /* Build the luav by or-ing in the type bits */
        ret = LLVMBuildPtrToInt(builder, ret, llvm_u64, "");
        Value tybits = LLVMConstInt(llvm_u64, LUAV_PACK(LTABLE, 0), FALSE);
        ret = LLVMBuildOr(builder, ret, tybits, "");

        /* Store and record the type */
        build_regset(&s, A(code), ret);
        SETTYPE(A(code), LTABLE);
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_SETTABLE: {
        STOP_ON(LTYPE(A(code)) != LTABLE, "bad SETTABLE");
        /* TODO: metatable? */
        Value av = TOPTR(LLVMBuildLoad(builder, regs[A(code)], ""));
        Value args[3] = {
          av,
          build_kregu(&s, B(code)),
          build_kregu(&s, C(code))
        };
        LLVMBuildCall(builder, llvm_lhash_set, args, 3, "");
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_GETTABLE: {
        STOP_ON(LTYPE(B(code)) != LTABLE, "bad GETTABLE (t:%d)", LTYPE(B(code)));
        Value table = TOPTR(build_reg(&s, B(code)));
        Value key = build_kregu(&s, C(code));
        int is_const = C(code) >= 256;
        build_lhash_get(&s, i - 1, table, key, is_const, A(code));
        /* TODO: guard for type of A */
        SETTYPE(A(code), func->trace.instrs[i - 1][0]);
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_TEST: {
        /* Figure out if regs[A(code)] is considered true, then branch */
        Value av         = build_reg(&s, A(code));
        Value isnt_nil   = LLVMBuildICmp(builder, LLVMIntNE, av, lvc_nil, "");
        Value isnt_false = LLVMBuildICmp(builder, LLVMIntNE, av, lvc_false, "");
        Value istrue     = LLVMBuildAnd(builder, isnt_nil, isnt_false, "");
        BasicBlock dst   = DSTBB(i);
        BasicBlock skip  = DSTBB(i + 1);

        if (C(code)) {
          LLVMBuildCondBr(builder, istrue, dst, skip);
        } else {
          LLVMBuildCondBr(builder, istrue, skip, dst);
        }
        break;
      }

      case OP_TESTSET: {
        /* Figure out if regs[B(code)] is considered true, then branch */
        Value bv         = build_reg(&s, B(code));
        Value isnt_nil   = LLVMBuildICmp(builder, LLVMIntNE, bv, lvc_nil, "");
        Value isnt_false = LLVMBuildICmp(builder, LLVMIntNE, bv, lvc_false, "");
        Value istrue     = LLVMBuildAnd(builder, isnt_nil, isnt_false, "");
        BasicBlock skip  = DSTBB(i + 1);
        BasicBlock dst   = LLVMAppendBasicBlock(function, "");

        if (C(code)) {
          LLVMBuildCondBr(builder, istrue, dst, skip);
        } else {
          LLVMBuildCondBr(builder, istrue, skip, dst);
        }

        /* On the 'not-skipped' branch, store the value */
        LLVMPositionBuilderAtEnd(builder, dst);
        build_regset(&s, A(code), bv);
        GOTOBB(i);

        /* TODO: guard */
        SETTYPE(A(code), func->trace.instrs[i - 1][0]);
        break;
      }

      case OP_LEN: {
        Value offset = NULL;
        switch (LTYPE(B(code))) {
          case LSTRING:
            offset = LLVMConstInt(llvm_u32, offsetof(lstring_t, length), FALSE);
            break;
          case LTABLE:
            offset = LLVMConstInt(llvm_u32, offsetof(lhash_t, length), FALSE);
            break;
          default:
            STOP_ON(1, "bad LEN");
        }
        /* Figure out the address of the 'length' field */
        Value ptr = TOPTR(build_reg(&s, B(code)));
        ptr = LLVMBuildInBoundsGEP(builder, ptr, &offset, 1, "");
        /* TODO: there must be a better way to do this... right? */
        Type ptr_type = sizeof(size_t) == 4 ? llvm_u32_ptr : llvm_u64_ptr;
        ptr = LLVMBuildBitCast(builder, ptr, ptr_type, "");

        /* Lengths are stored as 'u32', but the luav we return must be a double,
           so cast the u32 do a double, but then back to a u64 so it can go
           back into the u64 alloca location */
        Value len = LLVMBuildLoad(builder, ptr, "");
        len = LLVMBuildUIToFP(builder, len, llvm_double, "");
        len = LLVMBuildBitCast(builder, len, llvm_u64, "");
        build_regset(&s, A(code), len);
        SETTYPE(A(code), LNUMBER);
        GOTOBB(i);
        break;
      }

      case OP_SETLIST: {
        STOP_ON(LTYPE(A(code)) != LTABLE, "very bad SETLIST");
        /* If B == 0, then we call a C function to do the heavy lifting because
           everything is already on the stack anyway and it'd just be a pain to
           do this in LLVM */
        if (B(code) == 0) {
          /* Get the arguments for lhash_array */
          Value map = TOPTR(build_reg(&s, A(code)));
          Value stack = get_stack_base(base_addr, stacki, "");
          Value offset = LLVMConstInt(llvm_u32, A(code), FALSE);
          Value base = LLVMBuildInBoundsGEP(builder, stack, &offset, 1, "");
          Value endi = LLVMBuildLoad(builder, last_ret, "");
          endi = LLVMBuildSub(builder, endi,
                              LLVMConstInt(llvm_u32, A(code), FALSE), "");
          //endi = LLVMBuildSub(builder, endi, lvc_32_one, "");
          Value fn = LLVMGetNamedFunction(module, "lhash_array");
          Value args[3] = {map, base, endi};
          LLVMBuildCall(builder, fn, args, 3, "");
          GOTOBB(i);
          break;
        }

        /* Fetch the hash table, and prepare the arguments to lhash_set */
        u32 c = C(code);
        if (c == 0) {
          c = func->instrs[i++].instr;
        }
        Value fn  = LLVMGetNamedFunction(module, "lhash_set");
        Value tbl = TOPTR(build_reg(&s, A(code)));
        Value args[3] = {tbl, NULL, NULL};

        /* Call lhash_set, once per entry */
        for (j = 1; j <= B(code); j++) {
          u32 key = (c - 1) * LFIELDS_PER_FLUSH + j;
          args[1] = LLVMConstBitCast(LLVMConstReal(llvm_double, key), llvm_u64);
          args[2] = build_reg(&s, A(code) + j);
          LLVMBuildCall(builder, fn, args, 3, "");
        }
        GOTOBB(i);
        break;
      }

      case OP_GETUPVAL: {
        /* Load the luav of an upvalue */
        Value offset = LLVMConstInt(llvm_u32, B(code), FALSE);
        Value addr   = LLVMBuildInBoundsGEP(builder, upvalues, &offset, 1, "");
        Value upv    = TOPTR(LLVMBuildLoad(builder, addr, ""));
        /* Interpret the luav as an upvalue and load it */
        upv = LLVMBuildBitCast(builder, upv, llvm_u64_ptr, "");
        upv = LLVMBuildLoad(builder, upv, "");
        build_regset(&s, A(code), upv);
        SETTYPE(A(code), func->trace.instrs[i - 1][0]);
        GOTOBB(i);
        break;
      }

      case OP_SETUPVAL: {
        /* Load the luav of an upvalue */
        Value offset = LLVMConstInt(llvm_u32, B(code), FALSE);
        Value addr   = LLVMBuildInBoundsGEP(builder, upvalues, &offset, 1, "");
        Value upv    = TOPTR(LLVMBuildLoad(builder, addr, ""));
        /* Store register A into the pointer pointed to */
        upv = LLVMBuildBitCast(builder, upv, llvm_u64_ptr, "");
        LLVMBuildStore(builder, build_reg(&s, A(code)), upv);
        GOTOBB(i);
        break;
      }

      case OP_CLOSE: {
        for (j = A(code); j < func->max_stack; j++) {
          if (TRACE_ISUPVAL(regtyps[j])) {
            Value upv = build_reg(&s, j);
            /* Clear the upvalue information */
            regtyps[j] = (u8) (TYPE(j) & ~TRACE_UPVAL);
            build_regset(&s, j, upv);
          }
        }
        GOTOBB(i);
        break;
      }

      case OP_CLOSURE: {
        /* First, allocate the new closure */
        lfunc_t *child = func->funcs[BX(code)];
        Value fn = LLVMGetNamedFunction(module, "lclosure_alloc");
        Value args[2] = {closure, LLVMConstInt(llvm_u32, BX(code), FALSE)};
        Value closure2 = LLVMBuildCall(builder, fn, args, 2, "");
        Value upvalues2 = LLVMBuildInBoundsGEP(builder, closure2, &upv_off, 1,
                                               "");
        upvalues2 = LLVMBuildBitCast(builder, upvalues2, llvm_u64_ptr, "");

        /* Prepare all upvalues */
        fn = LLVMGetNamedFunction(module, "lupvalue_alloc");
        for (j = 0; j < child->num_upvalues; j++) {
          u32 pseudo = func->instrs[i++].instr;
          Value tostore = NULL;
          if (OP(pseudo) == OP_MOVE) {
            /* Using a register */
            tostore = LLVMBuildLoad(builder, regs[B(pseudo)], "");

            /* If we aren't already an upvalue, make an upvalue for ourselves */
            if (!TRACE_ISUPVAL(regtyps[B(pseudo)])) {
              tostore = LLVMBuildCall(builder, fn, &tostore, 1, "");
              LLVMBuildStore(builder, tostore, regs[B(pseudo)]);
              regtyps[B(pseudo)] |= TRACE_UPVAL;
            }
          } else {
            /* Using one of our upvalues */
            Value off = LLVMConstInt(llvm_u32, B(pseudo), FALSE);
            tostore = LLVMBuildInBoundsGEP(builder, upvalues, &off, 1, "");
            tostore = LLVMBuildLoad(builder, tostore, "");
          }
          /* Shove the upvalue into the closure's memory */
          Value offset = LLVMConstInt(llvm_u32, j, FALSE);
          Value addr   = LLVMBuildInBoundsGEP(builder, upvalues2, &offset, 1,
                                              "");
          LLVMBuildStore(builder, tostore, addr);
        }

        /* Pack our pointer and put it in the register */
        Value tybits = LLVMConstInt(llvm_u64, LUAV_PACK(LFUNCTION, 0), FALSE);
        closure2 = LLVMBuildPtrToInt(builder, closure2, llvm_u64, "");
        closure2 = LLVMBuildOr(builder, closure2, tybits, "");
        build_regset(&s, A(code), closure2);
        GOTOBB(i);
        break;
      }

      case OP_TAILCALL: {
        if (B(code) == 0) {
          warn("Bad OP_TAILCALL (B0)");
          EXIT_FAIL;
        }
        u32 num_args = B(code) - 1;
        u32 end_stores = A(code) + 1  + num_args;

        STOP_ON(LTYPE(A(code)) != LFUNCTION,
                "reall bad TAILCALL (%x)", LTYPE(A(code)));

        // copy arguments from c stack to lua stack
        u32 a = A(code);
        Value stack = get_stack_base(base_addr, stacki, "");
        for (j = a; j < end_stores; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = build_reg(&s, j);
          LLVMBuildStore(builder, val, addr);
        }

        LLVMBuildStore(builder, LLVMConstInt(llvm_u32, a, FALSE), argvia);
        LLVMBuildStore(builder, LLVMConstInt(llvm_u32, num_args, FALSE), argca);
        LLVMBuildRet(builder, LLVMConstInt(llvm_u32, (size_t) -1, TRUE));
        break;
      }

      case OP_CALL: {
        /* TODO: varargs, multiple returns, etc... */
        u32 num_args = B(code) - 1;
        u32 num_rets = C(code) - 1;
        u32 end_stores;
        if (B(code) == 0) {
          i32 tmp = get_varbase(&s, i);
          if (tmp < 0) { warn("B0 OP_CALL bad"); EXIT_FAIL; }
          end_stores = (u32) tmp;
        } else {
          end_stores = func->max_stack;
        }

        STOP_ON(LTYPE(A(code)) != LFUNCTION,
                "really bad CALL (%x)", LTYPE(A(code)));

        // copy arguments from c stack to lua stack
        u32 a = A(code);
        Value stack = get_stack_base(base_addr, stacki, "");
        /* TODO: figure out a better method for garbage collection to preserve
                 all of our alloca instances without storing everything */
        for (j = 0; j < end_stores; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = build_reg(&s, j);
          LLVMBuildStore(builder, val, addr);
        }

        // get the function pointer
        Value closure = TOPTR(build_reg(&s, a));

        // call the function
        Value av = LLVMConstInt(llvm_u32, a, FALSE);
        Value lnumargs;
        if (B(code) == 0) {
          Value tmp = LLVMBuildLoad(builder, last_ret, "");
          lnumargs = LLVMBuildSub(builder, tmp, av, "");
          lnumargs = LLVMBuildSub(builder, lnumargs, lvc_32_one, "");
        } else {
          lnumargs = LLVMConstInt(llvm_u32, num_args, FALSE);
        }
        Value args[] = {
          closure,
          parent,
          lnumargs,
          num_args == 0 ? lvc_32_one :
            LLVMBuildAdd(builder, stacki, LLVMConstAdd(av, lvc_32_one), ""),
          LLVMConstInt(llvm_u32, num_rets, FALSE),
          num_rets == 0 ? lvc_32_one :
            LLVMBuildAdd(builder, stacki, av, "")
        };
        Value ret = LLVMBuildCall(builder, llvm_vm_fun, args, 6, "");

        if (C(code) == 0) {
          LLVMBuildStore(builder, LLVMBuildAdd(builder, ret, av, ""), last_ret);
          GOTOBB(i);
          break;
        }

        BasicBlock load_regs    = LLVMAppendBasicBlock(function, "");
        BasicBlock failure_set  = LLVMAppendBasicBlock(function, "");
        BasicBlock failure_load = LLVMAppendBasicBlock(function, "");
        BasicBlock failure      = LLVMAppendBasicBlock(function, "");

        /* Figure out if we got the expected number of return values */
        stack = get_stack_base(base_addr, stacki, "");
        u32 want = func->trace.instrs[i - 1][0];
        xassert(want != TRACEMAX);
        Value expected = LLVMConstInt(llvm_u32, want, FALSE);
        Value cond = LLVMBuildICmp(builder, LLVMIntEQ, ret, expected, "");
        LLVMBuildCondBr(builder, cond, load_regs, failure);

        /* Load all return values and then go to the next basic block,
         * nilifying everything we wanted but we didn't get */
        LLVMPositionBuilderAtEnd(builder, load_regs);
        for (j = 0; j < want && j < num_rets; j++) {
          Value off  = LLVMConstInt(llvm_u64, a + j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = LLVMBuildLoad(builder, addr, "");
          build_regset(&s, a + j, val);
        }
        for (; j < num_rets; j++) {
          build_regset(&s, a + j, lvc_nil);
        }
        GOTOBB(i);

        /* Figure out if we need to memset */
        LLVMPositionBuilderAtEnd(builder, failure);
        cond = LLVMBuildICmp(builder, LLVMIntULT, ret,
                             LLVMConstInt(llvm_u32, num_rets, FALSE), "");
        LLVMBuildCondBr(builder, cond, failure_set, failure_load);

        /* Failure case, call memset with the correct arguments */
        LLVMPositionBuilderAtEnd(builder, failure_set);
        Value offset      = LLVMBuildAdd(builder, ret, av, "");
        Value memset_addr = LLVMBuildInBoundsGEP(builder, stack, &offset, 1, "");
        Value rets_wanted = LLVMConstInt(llvm_u32, num_rets, FALSE);
        Value memset_cnt  = LLVMBuildSub(builder, rets_wanted, ret, "");
        memset_cnt        = LLVMBuildMul(builder, memset_cnt, lvc_luav, "");
        Value memset      = LLVMGetNamedFunction(module, "llvm.memset.p0i8.i32");
        Value memset_args[5] = {
          LLVMBuildBitCast(builder, memset_addr, llvm_void_ptr, ""),
          LLVMConstInt(LLVMInt8Type(), 0xff, FALSE),
          memset_cnt,
          LLVMConstInt(llvm_u32, 8, FALSE),
          LLVMConstInt(LLVMInt1Type(), 0, FALSE)
        };
        LLVMBuildCall(builder, memset, memset_args, 5, "");
        LLVMBuildBr(builder, failure_load);

        /* Load all return values off the stack now */
        LLVMPositionBuilderAtEnd(builder, failure_load);
        for (j = a; j < a + num_rets; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = LLVMBuildLoad(builder, addr, "");
          build_regset(&s, j, val);
        }
        GOTOBB(i);

        for (j = a; j < a + num_rets; j++) {
          if (j - a >= TRACELIMIT) {
            SETTYPE(j, LANY);
          } else {
            SETTYPE(j, func->trace.instrs[i - 1][j - a + 1]);
          }
        }

#if 0
        /* Guard return values and bail out if we're wrong */
        for (j = a; j < a + num_rets; j++) {
          if (j - a >= TRACELIMIT) {
            regtyps[j] = LANY;
            continue;
          }

          Value      cond = NULL;
          Value      reg  = LLVMBuildLoad(builder, regs[j], "");
          BasicBlock next = LLVMInsertBasicBlock(DSTBB(i), "");
          u8         typ  = func->trace.instrs[i - 1][j - a];

          if (typ == LNUMBER) {
            /* First, check if any NaN bits aren't set */
            Value bits = LLVMBuildAnd(builder, reg, lvc_nan_mask, "");
            Value not_nan = LLVMBuildICmp(builder, LLVMIntNE, bits,
                                          lvc_nan_mask, "");

            /* Next, check if this is the machine NaN or inf */
            Value mask = LLVMConstInt(llvm_u64, UINT64_C(7) << LUAV_DATA_SIZE,
                                      FALSE);
            bits = LLVMBuildAnd(builder, reg, mask, "");
            Value isnt_other = LLVMBuildICmp(builder, LLVMIntEQ, bits,
                                             LLVMConstInt(llvm_u64, 0, FALSE),
                                             "");
            cond = LLVMBuildOr(builder, not_nan, isnt_other, "");
          } else if (typ != LANY) {
            Value bits = LLVMBuildAnd(builder, reg, lvc_type_mask, "");
            Value want = LLVMConstInt(llvm_u64, LUAV_PACK(typ, 0), FALSE);
            cond       = LLVMBuildICmp(builder, LLVMIntEQ, bits, want, "");
          }
          regtyps[j] = typ;

          if (cond != NULL) {
            LLVMBuildCondBr(builder, cond, next, BAILBB(i));
          } else {
            LLVMBuildBr(builder, next);
          }
          LLVMPositionBuilderAtEnd(builder, next);
        }

        GOTOBB(i);
#endif
        break;
      }

      case OP_VARARG: {
        /* TODO - guard return value types */

        /* Figure out where we should load things from */
        xassert(func->trace.instrs[i - 1][0] != TRACEMAX);
        Value params = LLVMConstInt(llvm_u32, func->num_parameters, FALSE);
        Value basi = LLVMBuildAdd(builder, argvi, params, "");
        Value base = get_stack_base(base_addr, basi, "");

        /* B == 0 => memcpy */
        if (B(code) == 0) {
          Value stack = get_stack_base(base_addr, stacki, "");
          Value dest  = LLVMConstInt(llvm_u32, A(code), FALSE);
          dest        = LLVMBuildInBoundsGEP(builder, stack, &dest, 1, "");
          Value cnt   = LLVMBuildSub(builder, argc, params, "");
          Value cond  = LLVMBuildICmp(builder, LLVMIntULT, argc, params, "");
          cnt         = LLVMBuildSelect(builder, cond, lvc_32_zero, cnt, "");

          /* TODO: conditionally call vm_stack_grow */
          /* memcpy(dest, base, ...) */
          Value args[5] = {
            LLVMBuildBitCast(builder, dest, llvm_void_ptr, ""),
            LLVMBuildBitCast(builder, base, llvm_void_ptr, ""),
            LLVMBuildMul(builder, cnt, lvc_luav, ""),
            LLVMConstInt(llvm_u32, 8, FALSE),
            LLVMConstInt(LLVMInt1Type(), 0, FALSE)
          };
          LLVMBuildCall(builder, llvm_memcpy, args, 5, "");
          Value lr = LLVMBuildAdd(builder, cnt,
                                  LLVMConstInt(llvm_u32, A(code), FALSE), "");
          LLVMBuildStore(builder, lr, last_ret);
          GOTOBB(i);
          break;
        }

        /* B != 0 => tracing */
        u32 limit = B(code) - 1;
        u32 targc = func->trace.instrs[i - 1][0];

        BasicBlock load = LLVMAppendBasicBlock(function, "");
        Value cond = LLVMBuildICmp(builder, LLVMIntEQ, argc,
                                   LLVMConstInt(llvm_u32, targc, FALSE), "");
        LLVMBuildCondBr(builder, cond, load, ERRBB(i - 1));
        u32 tmax = targc < func->num_parameters ? 0 :
                    targc - func->num_parameters;

        LLVMPositionBuilderAtEnd(builder, load);
        /* Load all args provided */
        for (j = 0; j < limit && j < tmax; j++) {
          Value joff = LLVMConstInt(llvm_u32, j, FALSE);
          Value addr = LLVMBuildInBoundsGEP(builder, base, &joff, 1, "");
          Value jval = LLVMBuildLoad(builder, addr, "");
          build_regset(&s, A(code) + j, jval);
          if (j < TRACELIMIT - 1) {
            SETTYPE(A(code) + j, func->trace.instrs[i - 1][j + 1]);
          } else {
            SETTYPE(A(code) + j, LANY);
          }
        }
        /* Nil-ify all arguments not provided */
        for (; j < limit; j++) {
          build_regset(&s, A(code) + j, lvc_nil);
          SETTYPE(A(code) + j, LNIL | TRACE_CONST);
        }
        GOTOBB(i);
        break;
      }

      case OP_CONCAT: {
        /* TODO: have a C function which takes a vector of strings and
                 concatenates them? */
        for (j = B(code); j <= C(code); j++) {
          STOP_ON(LTYPE(j) != LSTRING && LTYPE(j) != LNUMBER,
                  "bad CONCAT (%x)", LTYPE(j));
        }
        if (j != C(code) + 1) break;
        Value fn = LLVMGetNamedFunction(module, "lv_concat");
        Value args[2];

        Value cur = build_reg(&s, B(code));
        for (j = B(code) + 1; j <= C(code); j++) {
          args[0] = cur;
          args[1] = build_reg(&s, j);
          cur = LLVMBuildCall(builder, fn, args, 2, "");
        }

        build_regset(&s, A(code), cur);
        SETTYPE(A(code), LSTRING);
        /* TODO: gc_check */
        GOTOBB(i);
        break;
      }

      case OP_TFORLOOP: {
        u32 a = A(code);
        u32 c = C(code);
        u32 want = func->trace.instrs[i - 1][0];
        STOP_ON(LTYPE(a) != LFUNCTION, "bad TFORLOOP (%x)", LTYPE(a));
        STOP_ON(want == TRACEMAX, "big TFORLOOP");

        /* Put our arguments on to the lua stack */
        Value stack = get_stack_base(base_addr, stacki, "");
        for (j = 0; j < func->max_stack; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = build_reg(&s, j);
          LLVMBuildStore(builder, val, addr);
        }

        /* Generate the arguments to vm_fun */
        Value args[] = {
          TOPTR(build_reg(&s, a)),
          parent,
          LLVMConstInt(llvm_u32, 2, FALSE),
          LLVMBuildAdd(builder, stacki,
                       LLVMConstInt(llvm_u32, a + 1, FALSE), ""),
          LLVMConstInt(llvm_u32, c, FALSE),
          LLVMBuildAdd(builder, stacki,
                       LLVMConstInt(llvm_u32, a + 3, FALSE), "")
        };

        /* Invoke and check to see if we got what we want */
        Value ret = LLVMBuildCall(builder, llvm_vm_fun, args, 6, "");

        BasicBlock load_regs    = LLVMAppendBasicBlock(function, "");
        BasicBlock failure_set  = LLVMAppendBasicBlock(function, "");
        BasicBlock failure_load = LLVMAppendBasicBlock(function, "");
        BasicBlock failure      = LLVMAppendBasicBlock(function, "");
        BasicBlock test         = LLVMAppendBasicBlock(function, "");

        /* Figure out if we got the expected number of return values */
        stack = get_stack_base(base_addr, stacki, "");
        Value expected = LLVMConstInt(llvm_u32, want, FALSE);
        Value cond = LLVMBuildICmp(builder, LLVMIntEQ, ret, expected, "");
        LLVMBuildCondBr(builder, cond, load_regs, failure);

        /* Load all return values and then go to the next basic block,
         * nilifying everything we wanted but we didn't get */
        LLVMPositionBuilderAtEnd(builder, load_regs);
        for (j = 0; j < want && j < c; j++) {
          Value off  = LLVMConstInt(llvm_u64, a + 3 + j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = LLVMBuildLoad(builder, addr, "");
          build_regset(&s, a + 3 + j, val);
        }
        for (; j < c; j++) {
          build_regset(&s, a + 3 + j, lvc_nil);
        }
        LLVMBuildBr(builder, test);

        /* Figure out if we need to memset */
        LLVMPositionBuilderAtEnd(builder, failure);
        cond = LLVMBuildICmp(builder, LLVMIntULT, ret,
                             LLVMConstInt(llvm_u32, c, FALSE), "");
        LLVMBuildCondBr(builder, cond, failure_set, failure_load);

        /* Failure case, call memset with the correct arguments */
        LLVMPositionBuilderAtEnd(builder, failure_set);
        Value av          = LLVMConstInt(llvm_u32, a + 3, FALSE);
        Value offset      = LLVMBuildAdd(builder, ret, av, "");
        Value memset_addr = LLVMBuildInBoundsGEP(builder, stack, &offset, 1, "");
        Value rets_wanted = LLVMConstInt(llvm_u32, c, FALSE);
        Value memset_cnt  = LLVMBuildSub(builder, rets_wanted, ret, "");
        memset_cnt        = LLVMBuildMul(builder, memset_cnt, lvc_luav, "");
        Value memset      = LLVMGetNamedFunction(module, "llvm.memset.p0i8.i32");
        Value memset_args[5] = {
          LLVMBuildBitCast(builder, memset_addr, llvm_void_ptr, ""),
          LLVMConstInt(LLVMInt8Type(), 0xff, FALSE),
          memset_cnt,
          LLVMConstInt(llvm_u32, 8, FALSE),
          LLVMConstInt(LLVMInt1Type(), 0, FALSE)
        };
        LLVMBuildCall(builder, memset, memset_args, 5, "");
        LLVMBuildBr(builder, failure_load);

        /* Load all return values off the stack now */
        LLVMPositionBuilderAtEnd(builder, failure_load);
        for (j = a + 3; j < a + 3 + c; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = LLVMBuildLoad(builder, addr, "");
          build_regset(&s, j, val);
        }
        LLVMBuildBr(builder, test);

        for (j = a + 3; j < a + 3 + c; j++) {
          if (j - a >= TRACELIMIT) {
            SETTYPE(j, LANY);
          } else {
            SETTYPE(j, func->trace.instrs[i - 1][j - a + 1]);
          }
        }

        /* Perform the test that TFORLOOP does */
        BasicBlock seta2 = LLVMAppendBasicBlock(function, "");
        LLVMPositionBuilderAtEnd(builder, test);
        Value a3 = build_reg(&s, a + 3);
        cond = LLVMBuildICmp(builder, LLVMIntEQ, a3, lvc_nil, "");
        LLVMBuildCondBr(builder, cond, DSTBB(i + 1), seta2);

        /* Perform the final set for TFORLOOP */
        LLVMPositionBuilderAtEnd(builder, seta2);
        build_regset(&s, a + 2, a3);
        GOTOBB(i);
        break;
      }

      /* TODO - here are all the unimplemented opcodes */
      case OP_SELF:

      default:
        // TODO cleanup
        EXIT_FAIL;
    }
  }

  //LLVMDumpValue(function);
  LLVMRunFunctionPassManager(pass_manager, function);
  // LLVMDumpValue(function);
  fprintf(stderr, "compiled %d => %d (line:%d)\n", start, end, func->start_line);
  jfun->value = function;
  jfun->binary = LLVMGetPointerToGlobal(ex_engine, function);
  return 0;
}

Value build_pow(LLVMBuilderRef builder, Value bv, Value cv, const char* name) {
  Value fn = LLVMGetNamedFunction(module, "llvm.pow.f64");
  xassert(fn != NULL);
  Value args[] = {bv, cv};
  return LLVMBuildCall(builder, fn, args, 2, "");
}

/**
 * @brief Run a JIT-ed function
 *
 * @param function the function to run
 * @param closure the closure the function is being run for
 * @param stacki the base index of the stack
 *
 * @return the program counter which was bailed out on, or TODO: MORE HERE
 */
i32 llvm_run(jfunc_t *function, lclosure_t *closure, u32 *args) {
  jitf *f = (jitf*) &(function->binary);
  return f(closure, args);
}

/**
 * @brief Frees the given function
 *
 * @param func The function to delete
 * @return 0 on success, negative number on failure
 */
void llvm_free(jfunc_t *func) {
  if (func->value != NULL) {
    // TODO - does this actually delete everything?
    LLVMFreeMachineCodeForFunction(ex_engine, func->value);
    LLVMDeleteFunction(func->value);
  }
}
