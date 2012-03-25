/**
 * @file llvm.c
 * @brief Will eventually contain JIT-related compliation tied into LLVM
 */

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
  ((u8) ((idx) >= 256 ? lv_gettype(func->consts[(idx) - 256]) : regtyps[idx]))
#define warn(fmt, ...) fprintf(stderr, fmt "\n", ## __VA_ARGS__)
#define ADD_FUNCTION2(name, str, ret, numa, ...)                  \
  Type name##_args[numa] = {__VA_ARGS__};                         \
  Type name##_type = LLVMFunctionType(ret, name##_args, numa, 0); \
  LLVMAddFunction(module, str, name##_type)
#define ADD_FUNCTION(name, ret, numa, ...) \
  ADD_FUNCTION2(name, #name, ret, numa, __VA_ARGS__)

/* Global contexts for LLVM compilation */
static LLVMModuleRef module;
static LLVMPassManagerRef pass_manager;
static LLVMExecutionEngineRef ex_engine;
static LLVMBuilderRef builder;

static Type llvm_i32;
static Type llvm_u32;
static Type llvm_u64;
static Type llvm_double;
static Type llvm_double_ptr;
static Type llvm_void_ptr;
static Type llvm_void_ptr_ptr;

static Value lvc_null;
static Value lvc_u32_one;
static Value lvc_data_mask;
static Value lvc_type_mask;
static Value lvc_nan_mask;
static Value lvc_nil;

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
  llvm_double       = LLVMDoubleType();
  llvm_double_ptr   = LLVMPointerType(llvm_double, 0);
  llvm_void_ptr     = LLVMPointerType(LLVMInt8Type(), 0);
  llvm_void_ptr_ptr = LLVMPointerType(llvm_void_ptr, 0);

  /* Constants */
  lvc_null      = LLVMConstNull(llvm_void_ptr);
  lvc_u32_one   = LLVMConstInt(llvm_u32, 1, FALSE);
  lvc_data_mask = LLVMConstInt(llvm_u64, LUAV_DATA_MASK, FALSE);
  lvc_type_mask = LLVMConstInt(llvm_u64, LUAV_TYPE_MASK, FALSE);
  lvc_nan_mask  = LLVMConstInt(llvm_u64, LUAV_NAN_MASK, FALSE);
  lvc_nil       = LLVMConstInt(llvm_u64, LUAV_NIL, FALSE);

  /* Adding functions */
  ADD_FUNCTION(lhash_get, llvm_u64, 2, llvm_void_ptr, llvm_u64);
  ADD_FUNCTION(lhash_set, LLVMVoidType(), 3, llvm_void_ptr, llvm_u64, llvm_u64);
  ADD_FUNCTION(vm_fun, llvm_u32, 6, llvm_void_ptr, llvm_void_ptr, llvm_u32,
                                    llvm_u32, llvm_u32, llvm_u32);
  ADD_FUNCTION(memset, llvm_void_ptr, 3, llvm_void_ptr, llvm_u32, llvm_u64);
  ADD_FUNCTION2(llvm_pow, "llvm.pow.f64", llvm_double, 2, llvm_double,
                llvm_double);
  ADD_FUNCTION(lhash_hint, llvm_void_ptr, 2, llvm_u32, llvm_u32);
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
 * @brief Build a register access, which could actually be a constant access
 *
 * @param reg the number of the register to access
 * @param consts the array of constants
 * @param regs the array of registers
 *
 * @return the corresponding register value, as a u64
 */
static Value build_regu(u32 reg, Value *consts, Value *regs) {
  if (reg >= 256) {
    return consts[reg - 256];
  } else {
    return LLVMBuildLoad(builder, regs[reg], "");
  }
}

/**
 * @brief Build a register access, which could actually be a constant access
 *
 * @param reg the number of the register to access
 * @param consts the array of constants
 * @param regs the array of registers
 *
 * @return the corresponding register value, as a double
 */
static Value build_regf(u32 reg, Value *consts, Value *regs) {
  return LLVMBuildBitCast(builder, build_regu(reg, consts, regs),
                          llvm_double, "");
}

/**
 * @brief Build a binary operation of two values for a function
 *
 * Builds the LLVM instructions necessary to load the operands, perform the
 * operation, and then store the result.
 *
 * @param code the lua opcode
 * @param consts the constants array
 * @param regs the array of registers
 * @param operation the LLVM binary operation to perform
 */
static void build_binop(u32 code, Value *consts, Value *regs, Binop operation) {
  Value bv = build_regf(B(code), consts, regs);
  Value cv = build_regf(C(code), consts, regs);
  Value res = operation(builder, bv, cv, "");
  res = LLVMBuildBitCast(builder, res, llvm_u64, "");
  LLVMBuildStore(builder, res, regs[A(code)]);
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
  BasicBlock blocks[end + 1]; // 'start' blocks wasted
  BasicBlock bail_blocks[end + 1];
  Value regs[func->max_stack];
  Value consts[func->num_consts];
  Value ret_val;
  BasicBlock ret_block;
  u8    regtyps[func->max_stack];
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
  ret_val = LLVMBuildAlloca(builder, llvm_i32, "ret_val");

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

  /* Calculate closure->env */
  Value offset = LLVMConstInt(llvm_u64, offsetof(lclosure_t, env), 0);
  Value env_addr = LLVMBuildInBoundsGEP(builder, closure, &offset, 1,"");
  env_addr = LLVMBuildBitCast(builder, env_addr, llvm_void_ptr_ptr, "");
  Value closure_env = LLVMBuildLoad(builder, env_addr, "env");

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
  ret_block = LLVMAppendBasicBlock(function, "exit");
  LLVMPositionBuilderAtEnd(builder, ret_block);
  Value stack_ptr = get_stack_base(base_addr, stacki, "stack");
  for (i = 0; i < func->max_stack; i++) {
    Value off  = LLVMConstInt(llvm_u32, i, FALSE);
    Value addr = LLVMBuildInBoundsGEP(builder, stack_ptr, &off, 1, "");
    Value val  = LLVMBuildLoad(builder, regs[i], "");
    LLVMBuildStore(builder, val, addr);
  }
  Value r = LLVMBuildLoad(builder, ret_val, "");
  LLVMBuildRet(builder, r);

  /* Initialize the types of all stack members */
  for (i = 0; i < func->max_stack; i++) {
    regtyps[i] = lv_gettype(stack[i]);
  }

  /* Translate! */
  for (i = start; i <= end;) {
    LLVMPositionBuilderAtEnd(builder, blocks[i]);
    u32 code = func->instrs[i++].instr;

    switch (OP(code)) {
      case OP_MOVE: {
        Value val = LLVMBuildLoad(builder, regs[B(code)], "mv");
        LLVMBuildStore(builder, val, regs[A(code)]);
        regtyps[A(code)] = regtyps[B(code)];
        GOTOBB(i);
        break;
      }

      case OP_LOADK: {
        LLVMBuildStore(builder, consts[BX(code)], regs[A(code)]);
        regtyps[A(code)] = lv_gettype(func->consts[BX(code)]);
        GOTOBB(i);
        break;
      }

      case OP_LOADNIL: {
        for (j = A(code); j <= B(code); j++) {
          LLVMBuildStore(builder, lvc_nil, regs[j]);
          regtyps[j] = LNIL;
        }
        GOTOBB(i);
        break;
      }

      case OP_LOADBOOL: {
        Value bv = LLVMConstInt(llvm_u64, B(code) ? LUAV_TRUE : LUAV_FALSE, 0);
        LLVMBuildStore(builder, bv, regs[A(code)]);
        regtyps[A(code)] = LBOOLEAN;
        u32 next = C(code) ? i + 1 : i;
        GOTOBB(next);
        break;
      }

      case OP_NOT: {
        if (regtyps[B(code)] != LBOOLEAN) { warn("bad NOT"); return NULL; }
        Value bv = LLVMBuildLoad(builder, regs[B(code)], "");
        Value one = LLVMConstInt(llvm_u64, 1, FALSE);
        bv = LLVMBuildXor(builder, bv, one, "");
        LLVMBuildStore(builder, bv, regs[A(code)]);
        GOTOBB(i);
        regtyps[A(code)] = LBOOLEAN;
        break;
      }

      /* TODO: assumes floats */
      #define BINIMPL(f)                                                \
        if (TYPE(B(code)) != LNUMBER || TYPE(C(code)) != LNUMBER) {     \
          warn("bad arith: %d,%d at %d", TYPE(B(code)), TYPE(C(code)),  \
                                         __LINE__);                     \
          return NULL;                                                  \
        }                                                               \
        build_binop(code, consts, regs, f);                             \
        regtyps[A(code)] = LNUMBER;                                     \
        GOTOBB(i);
      case OP_ADD: BINIMPL(LLVMBuildFAdd)   break;
      case OP_SUB: BINIMPL(LLVMBuildFSub);  break;
      case OP_MUL: BINIMPL(LLVMBuildFMul);  break;
      case OP_DIV: BINIMPL(LLVMBuildFDiv);  break;
      case OP_MOD: BINIMPL(LLVMBuildFRem);  break;
      case OP_POW: BINIMPL(build_pow);      break;

      case OP_UNM: {
        if (regtyps[B(code)] != LNUMBER) { warn("bad UNM"); return NULL; }
        Value bv = LLVMBuildLoad(builder, regs[B(code)], "");
        bv = LLVMBuildBitCast(builder, bv, llvm_double, "");
        Value res = LLVMBuildFNeg(builder, bv, "");
        res = LLVMBuildBitCast(builder, res, llvm_u64, "");
        LLVMBuildStore(builder, res, regs[A(code)]);
        regtyps[A(code)] = LNUMBER;
        GOTOBB(i);
        break;
      }

      case OP_EQ: {
        u8 btyp = TYPE(B(code));
        u8 ctyp = TYPE(C(code));
        Value cond;
        if (btyp == LNUMBER && ctyp == LNUMBER) {
          Value bv  = build_regf(B(code), consts, regs);
          Value cv  = build_regf(C(code), consts, regs);
          cond      = LLVMBuildFCmp(builder,
                                     A(code) ? LLVMRealUNE : LLVMRealUEQ,
                                     bv, cv, "");
        } else if (btyp != LANY && ctyp != LANY) {
          Value bv  = build_regu(B(code), consts, regs);
          Value cv  = build_regu(C(code), consts, regs);
          cond      = LLVMBuildICmp(builder,
                                     A(code) ? LLVMIntNE : LLVMIntEQ,
                                     bv, cv, "");
        } else {
          warn("bad EQ");
          return NULL;
        }
        BasicBlock truebb  = DSTBB(i + 1);
        BasicBlock falsebb = DSTBB(i);
        LLVMBuildCondBr(builder, cond, truebb, falsebb);
        break;
      }

      #define CMP_OP(cond1, cond2) {                                        \
          if (TYPE(B(code)) != LNUMBER || TYPE(C(code)) != LNUMBER) {       \
            warn("bad LT/LE");                                              \
            return NULL;                                                    \
          }                                                                 \
          Value bv = build_regf(B(code), consts, regs);                     \
          Value cv = build_regf(C(code), consts, regs);                     \
          LLVMRealPredicate pred = A(code) ? cond1 : cond2;                 \
          Value cond = LLVMBuildFCmp(builder, pred, bv, cv, "");            \
                                                                            \
          BasicBlock truebb  = DSTBB(i + 1);                                \
          BasicBlock falsebb = DSTBB(i);                                    \
          LLVMBuildCondBr(builder, cond, truebb, falsebb);                  \
        }
      case OP_LT: CMP_OP(LLVMRealUGE, LLVMRealULT); break;
      case OP_LE: CMP_OP(LLVMRealUGT, LLVMRealULE); break;

      case OP_JMP: {
        GOTOBB((u32) ((i32) i + SBX(code)));
        break;
      }

      case OP_RETURN: {
        if (B(code) == 0) { warn("bad RETURN"); return NULL; }
        Value ret_stack = get_stack_base(base_addr, retvi, "retstack");

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
          Value val  = LLVMBuildLoad(builder, regs[A(code) + j], "");
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
        LLVMBuildStore(builder, val, regs[A(code)]);
        /* TODO: guard this */
        regtyps[A(code)] = GET_TRACETYPE(func->trace.instrs[i - 1], 0);
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
          LLVMBuildLoad(builder, regs[A(code)], "")
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
        LLVMBuildStore(builder, ret, regs[A(code)]);
        regtyps[A(code)] = LTABLE;
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_SETTABLE: {
        if (regtyps[A(code)] != LTABLE) { warn("bad SETTABLE"); return NULL; }
        /* TODO: metatable? */
        Value fn = LLVMGetNamedFunction(module, "lhash_set");
        Value av = LLVMBuildLoad(builder, regs[A(code)], "");
        av = LLVMBuildAnd(builder, av, lvc_data_mask, "");
        av = LLVMBuildIntToPtr(builder, av, llvm_void_ptr, "");
        Value args[3] = {
          av,
          build_regu(B(code), consts, regs),
          build_regu(C(code), consts, regs)
        };
        LLVMBuildCall(builder, fn, args, 3, "");
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_GETTABLE: {
        if (regtyps[B(code)] != LTABLE) { warn("bad GETTABLE"); return NULL; }
        /* TODO: metatable? */
        Value fn = LLVMGetNamedFunction(module, "lhash_get");
        Value bv = LLVMBuildLoad(builder, regs[B(code)], "");
        bv = LLVMBuildAnd(builder, bv, lvc_data_mask, "");
        bv = LLVMBuildIntToPtr(builder, bv, llvm_void_ptr, "");
        Value args[2] = {bv, build_regu(C(code), consts, regs)};
        Value ref = LLVMBuildCall(builder, fn, args, 2, "");
        LLVMBuildStore(builder, ref, regs[A(code)]);
        regtyps[A(code)] = GET_TRACETYPE(func->trace.instrs[i - 1], 0);
        /* TODO: guard for type of A */
        /* TODO: gc_check() */
        GOTOBB(i);
        break;
      }

      case OP_CALL: {
        /* TODO: varargs, multiple returns, etc... */
        if (B(code) == 0 || C(code) == 0) { warn("bad CALL"); return NULL; }
        u32 num_args = B(code) - 1;
        u32 num_rets = C(code) - 1;

        // copy arguments from c stack to lua stack
        u32 a = A(code);
        Value stack = get_stack_base(base_addr, stacki, "");
        for (j = a + 1; j < a + 1 + num_args; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = LLVMBuildLoad(builder, regs[j], "");
          LLVMBuildStore(builder, val, addr);
        }

        // get the function pointer
        Value closure = LLVMBuildLoad(builder, regs[a], "f");
        closure = LLVMBuildAnd(builder, closure, lvc_data_mask, "");
        closure = LLVMBuildIntToPtr(builder, closure, llvm_void_ptr, "");

        // call the function
        Value av = LLVMConstInt(llvm_u32, a, FALSE);
        Value fn = LLVMGetNamedFunction(module, "vm_fun");
        xassert(fn != NULL);
        Value args[] = {
          closure,
          parent,
          LLVMConstInt(llvm_u32, num_args, FALSE),
          num_args == 0 ? lvc_u32_one :
            LLVMBuildAdd(builder, stacki, LLVMConstAdd(av, lvc_u32_one), ""),
          LLVMConstInt(llvm_u32, num_rets, FALSE),
          num_rets == 0 ? lvc_u32_one :
            LLVMBuildAdd(builder, stacki, av, "")
        };
        Value ret = LLVMBuildCall(builder, fn, args, 6, "");

        /* Set all remaining parameters to nil with a loop */
        Value reta = LLVMBuildAlloca(builder, llvm_u32, "ret");
        LLVMBuildStore(builder, ret, reta);
        stack = get_stack_base(base_addr, stacki, "");

        BasicBlock load_regs = LLVMAppendBasicBlock(function, "");
        BasicBlock loop_cond = LLVMAppendBasicBlock(function, "");
        BasicBlock loop = LLVMAppendBasicBlock(function, "");
        LLVMBuildBr(builder, loop_cond);

        /* Check if we've reached the desired number of params */
        LLVMPositionBuilderAtEnd(builder, loop_cond);
        Value match = LLVMBuildICmp(builder, LLVMIntUGE,
                                    LLVMBuildLoad(builder, reta, ""),
                                    LLVMConstInt(llvm_u32, num_rets, FALSE),
                                    "");
        LLVMBuildCondBr(builder, match, load_regs, loop);

        /* Store a nil, increment the number of returns, and loop back */
        LLVMPositionBuilderAtEnd(builder, loop);
        Value cur = LLVMBuildLoad(builder, reta, "");
        Value addr = LLVMBuildInBoundsGEP(builder, stack, &cur, 1, "");
        LLVMBuildStore(builder, lvc_nil, addr);
        LLVMBuildStore(builder, LLVMBuildAdd(builder, cur, lvc_u32_one, ""),
                       reta);
        LLVMBuildBr(builder, loop_cond);

        /* copy return values from lua stack back to c stack */
        LLVMPositionBuilderAtEnd(builder, load_regs);
        for (j = a; j < a + num_rets; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = LLVMBuildLoad(builder, addr, "");
          LLVMBuildStore(builder, val, regs[j]);
        }

        /* Guard return values and bail out if we're wrong */
        for (j = a; j < a + num_rets; j++) {
          if (j - a >= TRACELIMIT) {
            regtyps[j] = LANY;
          }

          Value      cond = NULL;
          Value      reg  = LLVMBuildLoad(builder, regs[j], "");
          BasicBlock next = LLVMInsertBasicBlock(DSTBB(i), "");
          u8         typ  = GET_TRACETYPE(func->trace.instrs[i - 1], j - a);

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
        break;
      }

      /* TODO - here are all the unimplemented opcodes */
      case OP_GETUPVAL:
      case OP_SETUPVAL:
      case OP_SELF:
      case OP_LEN:
      case OP_CONCAT:
      case OP_TEST:
      case OP_TESTSET:
      case OP_TAILCALL:
      case OP_FORLOOP:
      case OP_FORPREP:
      case OP_TFORLOOP:
      case OP_SETLIST:
      case OP_CLOSE:
      case OP_CLOSURE:
      case OP_VARARG:

      default:
        // TODO cleanup
        return NULL;
    }
  }

  LLVMRunFunctionPassManager(pass_manager, function);
  // LLVMDumpValue(function);
  // warn("compiled %d => %d", start, end);
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
