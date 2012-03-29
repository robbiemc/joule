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
#define BAILBB(idx) ({                                              \
    BasicBlock tmp = bail_blocks[idx];                              \
    if (tmp == NULL) {                                              \
      BasicBlock cur = LLVMGetInsertBlock(builder);                 \
      tmp = LLVMInsertBasicBlock(ret_block, "");                    \
      LLVMPositionBuilderAtEnd(builder, tmp);                       \
      RETURN((i32) (idx));                                          \
      LLVMPositionBuilderAtEnd(builder, cur);                       \
      bail_blocks[idx] = tmp;                                       \
    }                                                               \
    tmp;                                                            \
  })
#define RETURN(ret)                                                   \
  Value r = LLVMConstInt(llvm_i32, (long long unsigned) (ret), TRUE); \
  LLVMBuildStore(builder, r, ret_val);                                \
  LLVMBuildBr(builder, ret_block);
#define TYPE(idx) \
  ((u8) ((idx) >= 256 ? lv_gettype(func->consts[(idx) - 256]) : \
                        (regtyps[idx] & ~TRACE_UPVAL)))
#define SETTYPE(idx, typ) \
  regtyps[idx] = (u8) ((regtyps[idx] & ~TRACE_TYPEMASK) | (typ))
#define TOPTR(v) ({                                             \
    Value __tmp = LLVMBuildAnd(builder, v, lvc_data_mask, "");  \
    LLVMBuildIntToPtr(builder, __tmp, llvm_void_ptr, "");       \
  })
#define warn(fmt, ...) fprintf(stderr, "[pc:%d, line:%d](%d => %d) " fmt "\n", \
                               i - 1, func->lines[i - 1], start, end, ## __VA_ARGS__)
#define ADD_FUNCTION2(name, str, ret, numa, ...)                  \
  Type name##_args[numa] = {__VA_ARGS__};                         \
  Type name##_type = LLVMFunctionType(ret, name##_args, numa, 0); \
  LLVMAddFunction(module, str, name##_type)
#define ADD_FUNCTION(name, ret, numa, ...) \
  ADD_FUNCTION2(name, #name, ret, numa, __VA_ARGS__)

typedef struct state {
  Value   *regs;
  Value   *consts;
  u8      *types;
  lfunc_t *func;
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
static Value lvc_data_mask;
static Value lvc_type_mask;
static Value lvc_nan_mask;
static Value lvc_nil;
static Value lvc_false;

Value build_pow(LLVMBuilderRef builder, Value bv, Value cv, const char* name);

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
  lvc_data_mask = LLVMConstInt(llvm_u64, LUAV_DATA_MASK, FALSE);
  lvc_type_mask = LLVMConstInt(llvm_u64, LUAV_TYPE_MASK, FALSE);
  lvc_nan_mask  = LLVMConstInt(llvm_u64, LUAV_NAN_MASK, FALSE);
  lvc_nil       = LLVMConstInt(llvm_u64, LUAV_NIL, FALSE);
  lvc_false     = LLVMConstInt(llvm_u64, LUAV_FALSE, FALSE);

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
}

/**
 * @brief Deallocates all memory associated with LLVM allocated on startup
 */
void llvm_destroy() {
  LLVMFinalizeFunctionPassManager(pass_manager);
  LLVMDisposePassManager(pass_manager);
  LLVMDisposeBuilder(builder);
  LLVMDisposeExecutionEngine(ex_engine);
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
jfunc_t* llvm_compile(lfunc_t *func, u32 start, u32 end, luav *stack) {
  BasicBlock blocks[func->num_instrs];
  BasicBlock bail_blocks[func->num_instrs];
  Value regs[func->max_stack];
  Value consts[func->num_consts];
  u8    regtyps[func->max_stack];
  state_t s = {
    .regs   = regs,
    .consts = consts,
    .types  = regtyps,
    .func   = func
  };
  char name[20];
  u32 i, j;
  Type params[2] = {llvm_void_ptr, LLVMPointerType(llvm_u32, 0)};

  Type  funtyp   = LLVMFunctionType(llvm_u32, params, 2, FALSE);
  Value function = LLVMAddFunction(module, "test", funtyp);
  Value closure  = LLVMGetParam(function, 0);
  Value jargs    = LLVMGetParam(function, 1);

  BasicBlock startbb = LLVMAppendBasicBlock(function, "start");
  LLVMPositionBuilderAtEnd(builder, startbb);
  /* Create the blocks and allocas */
  memset(blocks, 0, sizeof(blocks));
  memset(bail_blocks, 0, sizeof(bail_blocks));
  for (i = start; i <= end; i++) {
    sprintf(name, "block%d", i);
    blocks[i] = LLVMAppendBasicBlock(function, name);
  }
  for (i = 0; i < func->num_consts; i++) {
    consts[i] = LLVMConstInt(llvm_u64, func->consts[i], FALSE);
  }
  for (i = 0; i < func->max_stack; i++) {
    regs[i] = LLVMBuildAlloca(builder, llvm_u64, "");
  }
  Value ret_val  = LLVMBuildAlloca(builder, llvm_i32, "ret_val");
  Value last_ret = LLVMBuildAlloca(builder, llvm_i32, "last_ret");

  /* Calculate stacki, and LSTATE */
  Value stackio = LLVMConstInt(llvm_u32, JSTACKI, FALSE);
  Value stackia = LLVMBuildInBoundsGEP(builder, jargs, &stackio, 1, "");
  Value stacki  = LLVMBuildLoad(builder, stackia, "stacki");
  Value retco   = LLVMConstInt(llvm_u32, JRETC, FALSE);
  Value retca   = LLVMBuildInBoundsGEP(builder, jargs, &retco, 1, "");
  Value retc    = LLVMBuildLoad(builder, retca, "retc");
  Value retvio  = LLVMConstInt(llvm_u32, JRETVI, FALSE);
  Value retvia  = LLVMBuildInBoundsGEP(builder, jargs, &retvio, 1, "");
  Value retvi   = LLVMBuildLoad(builder, retvia, "retvi");
  Value argco   = LLVMConstInt(llvm_u32, JARGC, FALSE);
  Value argca   = LLVMBuildInBoundsGEP(builder, jargs, &argco, 1, "");
  Value argc    = LLVMBuildLoad(builder, argca, "argc");
  Value argvio  = LLVMConstInt(llvm_u32, JARGVI, FALSE);
  Value argvia  = LLVMBuildInBoundsGEP(builder, jargs, &argvio, 1, "");
  Value argvi   = LLVMBuildLoad(builder, argvia, "argvi");

  /* Calculate closure->env */
  Value offset = LLVMConstInt(llvm_u64, offsetof(lclosure_t, env), 0);
  Value env_addr = LLVMBuildInBoundsGEP(builder, closure, &offset, 1,"");
  env_addr = LLVMBuildBitCast(builder, env_addr, llvm_void_ptr_ptr, "");
  Value closure_env = LLVMBuildLoad(builder, env_addr, "env");

  /* Load last_ret */
  offset = LLVMConstInt(llvm_u64, offsetof(lclosure_t, last_ret), 0);
  Value last_ret_addr = LLVMBuildInBoundsGEP(builder, closure, &offset, 1,"");
  last_ret_addr = LLVMBuildBitCast(builder, last_ret_addr, llvm_u32_ptr, "");
  LLVMBuildStore(builder, LLVMBuildLoad(builder, last_ret_addr, ""), last_ret);

  /* Calculate &closure->upvalues */
  Value upv_off  = LLVMConstInt(llvm_u64, offsetof(lclosure_t, upvalues), 0);
  Value upv_addr = LLVMBuildInBoundsGEP(builder, closure, &upv_off, 1,"");
  Value upvalues = LLVMBuildBitCast(builder, upv_addr, llvm_u64_ptr, "");

  /* Calculate the currently running frame (parent of all other invocations) */
  Value parent = LLVMConstInt(llvm_u64, (size_t) &vm_running, FALSE);
  parent = LLVMConstIntToPtr(parent, llvm_void_ptr_ptr);
  parent = LLVMBuildLoad(builder, parent, "parent");

  /* Load address of vm_stack->base points to */
  Value vm_stack_ptr = LLVMConstInt(llvm_u64, (size_t) &vm_stack, FALSE);
  vm_stack_ptr       = LLVMConstIntToPtr(vm_stack_ptr, llvm_void_ptr_ptr);
  Value base_addr    = LLVMBuildLoad(builder, vm_stack_ptr, "");
  Value base_offset  = LLVMConstInt(llvm_u32, offsetof(lstack_t, base), FALSE);
  base_addr          = LLVMBuildInBoundsGEP(builder, base_addr, &base_offset, 1,
                                            "");
  Type stack_typ     = LLVMPointerType(LLVMPointerType(llvm_u64, 0), 0);
  base_addr          = LLVMBuildBitCast(builder, base_addr, stack_typ, "");

  /* Copy the lua stack onto the C stack */
  Value lstack = get_stack_base(base_addr, stacki, "lstack");
  for (i = 0; i < func->max_stack; i++) {
    Value off  = LLVMConstInt(llvm_u32, i, 0);
    Value addr = LLVMBuildInBoundsGEP(builder, lstack, &off, 1, "");
    Value val  = LLVMBuildLoad(builder, addr, "");
    LLVMBuildStore(builder, val, regs[i]);
  }

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
        SETTYPE(A(code), lv_gettype(func->consts[BX(code)]));
        GOTOBB(i);
        break;
      }

      case OP_LOADNIL: {
        for (j = A(code); j <= B(code); j++) {
          build_regset(&s, j, lvc_nil);
          SETTYPE(j, LNIL);
        }
        GOTOBB(i);
        break;
      }

      case OP_LOADBOOL: {
        Value bv = LLVMConstInt(llvm_u64, B(code) ? LUAV_TRUE : LUAV_FALSE, 0);
        build_regset(&s, A(code), bv);
        SETTYPE(A(code), LBOOLEAN);
        u32 next = C(code) ? i + 1 : i;
        GOTOBB(next);
        break;
      }

      case OP_NOT: {
        if (TYPE(B(code)) != LBOOLEAN) { warn("bad NOT"); return NULL; }
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
        if (TYPE(B(code)) != LNUMBER || TYPE(C(code)) != LNUMBER) {           \
          warn("bad arith: %d,%d", TYPE(B(code)), TYPE(C(code)));             \
          return NULL;                                                        \
        }                                                                     \
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
        if (TYPE(B(code)) != LNUMBER) { warn("bad UNM"); return NULL; }
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
        u8 btyp = TYPE(B(code));
        u8 ctyp = TYPE(C(code));
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
          warn("bad EQ (%d, %d)", btyp, ctyp);
          return NULL;
        }
        BasicBlock truebb  = DSTBB(i + 1);
        BasicBlock falsebb = DSTBB(i);
        LLVMBuildCondBr(builder, cond, truebb, falsebb);
        break;
      }

      #define CMP_OP(cond1, cond2) {                                        \
          BasicBlock truebb  = DSTBB(i + 1);                                \
          BasicBlock falsebb = DSTBB(i);                                    \
          if (TYPE(B(code)) == LNUMBER && TYPE(C(code)) == LNUMBER) {       \
            Value bv = build_kregf(&s, B(code));                            \
            Value cv = build_kregf(&s, C(code));                            \
            LLVMRealPredicate pred = A(code) ? LLVMRealU##cond1             \
                                             : LLVMRealU##cond2;            \
            Value cond = LLVMBuildFCmp(builder, pred, bv, cv, "");          \
            LLVMBuildCondBr(builder, cond, truebb, falsebb);                \
          } else if (TYPE(B(code)) == LSTRING && TYPE(C(code)) == LSTRING) {\
            Value bv = TOPTR(build_kregu(&s, B(code)));                     \
            Value cv = TOPTR(build_kregu(&s, C(code)));                     \
            LLVMIntPredicate pred = A(code) ? LLVMIntS##cond1               \
                                            : LLVMIntS##cond2;              \
            Value fn = LLVMGetNamedFunction(module, "lstr_compare");        \
            xassert(fn != NULL);                                            \
            Value args[2] = {bv, cv};                                       \
            Value r  = LLVMBuildCall(builder, fn, args, 2, "lstr_compare"); \
            Value cond = LLVMBuildICmp(builder, pred, r, lvc_32_zero, "");  \
            LLVMBuildCondBr(builder, cond, truebb, falsebb);                \
          } else {                                                          \
            /* TODO - metatable */                                          \
            warn("bad LT/LE (%d, %d)", TYPE(B(code)), TYPE(C(code)));       \
            return NULL;                                                    \
          }                                                                 \
        }
      case OP_LT: CMP_OP(GE, LT); break;
      case OP_LE: CMP_OP(GT, LE); break;

      case OP_JMP: {
        GOTOBB((u32) ((i32) i + SBX(code)));
        break;
      }

      case OP_FORLOOP: {
        if (TYPE(A(code)) != LNUMBER || TYPE(A(code) + 1) != LNUMBER ||
                                        TYPE(A(code) + 2) != LNUMBER) {
          warn("bad FORLOOP");
          return NULL;
        }
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
        if (TYPE(A(code)) != LNUMBER || TYPE(A(code) + 2) != LNUMBER) {
          warn("bad FORPREP");
          return NULL;
        }
        /* TODO - guard that R(A) and R(A+2) are numbers */
        Value a2v = build_kregf(&s, A(code) + 2);
        Value av  = build_kregf(&s, A(code));
        av = LLVMBuildFSub(builder, av, a2v, "");
        build_regset(&s, A(code), LLVMBuildBitCast(builder, av, llvm_u64, ""));
        GOTOBB((u32) ((i32) i + SBX(code)));
        break;
      }

      case OP_RETURN: {
        Value ret_stack = get_stack_base(base_addr, retvi, "retstack");
        if (B(code) == 0) {
          Value stack = get_stack_base(base_addr, stacki, "");
          if (i - 1 == start) { warn("B0 return on first instr"); return NULL; }
          if (OP(func->instrs[i - 2].instr) != OP_CALL) {
            warn("B0 return where prev wasn't CALL");
            return NULL;
          }
          if (C(func->instrs[i - 2].instr) != 0) {
            warn("B0 return wasn't preceded with a C0 CALL");
            return NULL;
          }
          /* Store remaining registers onto our lua stack */
          u32 end_stores = A(func->instrs[i - 2].instr);
          for (j = A(code); j < end_stores; j++) {
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
          Value memmove = LLVMGetNamedFunction(module,
                                               "llvm.memmove.p0i8.p0i8.i32");
          Value args[5] = {
            LLVMBuildBitCast(builder, ret_stack, llvm_void_ptr, ""),
            LLVMBuildBitCast(builder, ret_base, llvm_void_ptr, ""),
            LLVMBuildMul(builder, amt,
                         LLVMConstInt(llvm_u32, sizeof(luav), FALSE), ""),
            LLVMConstInt(llvm_u32, 8, FALSE),
            LLVMConstInt(LLVMInt1Type(), 0, FALSE)
          };
          LLVMBuildCall(builder, memmove, args, 5, "");
          num_rets = LLVMBuildNeg(builder, num_rets, "");
          num_rets = LLVMBuildSub(builder, num_rets, lvc_32_one, "");
          LLVMBuildRet(builder, num_rets);
          break;
        }

        /* Create actual return first, so everything can jump to it */
        BasicBlock endbb = LLVMAppendBasicBlock(function, "end");
        LLVMPositionBuilderAtEnd(builder, endbb);
        u32 num_ret = B(code) - 1;
        LLVMBuildRet(builder, LLVMConstInt(llvm_u32, (u32)(-num_ret - 1),
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
        /* TODO: metatable? */
        Value fn = LLVMGetNamedFunction(module, "lhash_get");
        xassert(fn != NULL);
        Value args[2] = {closure_env, consts[BX(code)]};
        lstring_t *str = lv_getptr(func->consts[BX(code)]);
        Value val = LLVMBuildCall(builder, fn, args, 2, str->data);
        build_regset(&s, A(code), val);
        /* TODO: guard this */
        SETTYPE(A(code), func->trace.instrs[i - 1][0]);
        GOTOBB(i);
        break;
      }

      case OP_SETGLOBAL: {
        /* TODO: metatable? */
        Value fn = LLVMGetNamedFunction(module, "lhash_set");
        xassert(fn != NULL);
        Value args[3] = {
          closure_env,
          consts[BX(code)],
          build_reg(&s, A(code))
        };
        LLVMBuildCall(builder, fn, args, 3, "");
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
        if (TYPE(A(code)) != LTABLE) { warn("bad SETTABLE"); return NULL; }
        /* TODO: metatable? */
        Value fn = LLVMGetNamedFunction(module, "lhash_set");
        Value av = TOPTR(LLVMBuildLoad(builder, regs[A(code)], ""));
        Value args[3] = {
          av,
          build_kregu(&s, B(code)),
          build_kregu(&s, C(code))
        };
        LLVMBuildCall(builder, fn, args, 3, "");
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_GETTABLE: {
        if (TYPE(B(code)) != LTABLE) {
          warn("bad GETTABLE (typ:%d)", TYPE(B(code)));
          return NULL;
        }
        /* TODO: metatable? */
        Value fn = LLVMGetNamedFunction(module, "lhash_get");
        Value bv = TOPTR(LLVMBuildLoad(builder, regs[B(code)], ""));
        Value args[2] = {bv, build_kregu(&s, C(code))};
        Value ref = LLVMBuildCall(builder, fn, args, 2, "");
        build_regset(&s, A(code), ref);
        SETTYPE(A(code), func->trace.instrs[i - 1][0]);
        /* TODO: guard for type of A */
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
        BasicBlock dst   = LLVMAppendBasicBlock(function, "");
        BasicBlock skip  = DSTBB(i + 1);

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
        switch (TYPE(B(code))) {
          case LSTRING:
            offset = LLVMConstInt(llvm_u32, offsetof(lstring_t, length), FALSE);
            break;
          case LTABLE:
            offset = LLVMConstInt(llvm_u32, offsetof(lhash_t, length), FALSE);
            break;
          default:
            warn("bad LEN");
            return NULL;
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
        if (B(code) == 0) { warn("bad SETLIST"); return NULL; }
        if (TYPE(A(code)) != LTABLE) { warn("very bad SETLIST"); return NULL; }

        /* Fetch the hash table, and prepare the arguments to lhash_set */
        u32 c = C(code);
        if (c == 0) {
          LLVMDeleteBasicBlock(blocks[i]);
          blocks[i] = NULL;
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
            regtyps[j] = TYPE(j); /* Clear the upvalue information */
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
          LLVMDeleteBasicBlock(blocks[i]);
          blocks[i] = NULL;
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

      case OP_CALL: {
        /* TODO: varargs, multiple returns, etc... */
        u32 num_args = B(code) - 1;
        u32 num_rets = C(code) - 1;
        u32 end_stores;
        if (B(code) == 0) {
          if (i - 1 == start) { warn("B0 call on first instr"); return NULL; }
          if (OP(func->instrs[i - 2].instr) != OP_CALL) {
            warn("B0 call where prev wasn't CALL");
            return NULL;
          }
          if (C(func->instrs[i - 2].instr) != 0) {
            warn("B0 CALL wasn't preceded with a C0 CALL");
            return NULL;
          }
          end_stores = A(func->instrs[i - 2].instr);
        } else {
          end_stores = A(code) + 1 + num_args;
        }

        if (TYPE(A(code)) != LFUNCTION) {
          warn("really bad CALL (%x)", TYPE(A(code))); return NULL;
        }

        // copy arguments from c stack to lua stack
        u32 a = A(code);
        Value stack = get_stack_base(base_addr, stacki, "");
        for (j = a + 1; j < end_stores; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = build_reg(&s, j);
          LLVMBuildStore(builder, val, addr);
        }

        // get the function pointer
        Value closure = TOPTR(build_reg(&s, a));

        // call the function
        Value av = LLVMConstInt(llvm_u32, a, FALSE);
        Value fn = LLVMGetNamedFunction(module, "vm_fun");
        xassert(fn != NULL);
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
        Value ret = LLVMBuildCall(builder, fn, args, 6, "");

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
        Value scalar      = LLVMConstInt(llvm_u32, sizeof(luav), FALSE);
        memset_cnt        = LLVMBuildMul(builder, memset_cnt, scalar, "");
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
        if (B(code) == 0) {
          // TODO B == 0 case
          warn("vararg B == 0");
          return NULL;
        }

        /* Figure out where we should load things from */
        xassert(func->trace.instrs[i - 1][0] != TRACEMAX);
        Value num_params = LLVMConstInt(llvm_u32, func->num_parameters, FALSE);
        Value basi = LLVMBuildAdd(builder, argvi, num_params, "");
        Value base = get_stack_base(base_addr, basi, "");
        u32 limit = B(code) - 1;
        u32 targc = func->trace.instrs[i - 1][0];

        BasicBlock load = LLVMAppendBasicBlock(function, "");
        Value cond = LLVMBuildICmp(builder, LLVMIntEQ, argc,
                                   LLVMConstInt(llvm_u32, targc, FALSE), "");
        LLVMBuildCondBr(builder, cond, load, BAILBB(i - 1));
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
          SETTYPE(A(code) + j, LNIL);
        }
        GOTOBB(i);
        break;
      }

      /* TODO - here are all the unimplemented opcodes */
      case OP_SELF:
      case OP_CONCAT:
      case OP_TAILCALL:
      case OP_TFORLOOP:

      default:
        // TODO cleanup
        return NULL;
    }
  }

  // LLVMDumpValue(function);
  LLVMRunFunctionPassManager(pass_manager, function);
  // LLVMDumpValue(function);
  // warn("compiled");
  return LLVMGetPointerToGlobal(ex_engine, function);
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
  jitf *f = function;
  return f(closure, args);
}
