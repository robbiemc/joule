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

typedef LLVMValueRef Value;
typedef LLVMTypeRef  Type;
typedef LLVMValueRef(Binop)(LLVMBuilderRef, Value, Value, const char*);
typedef i32(jitf)(void*, void*);

#define GOTOBB(idx) {                                                       \
    if ((idx) > end || (idx) < start) {                                     \
      llvm_build_return((i32) (idx), func->max_stack, regs, base_addr);     \
    } else {                                                                \
      LLVMBuildBr(builder, blocks[idx]);                                    \
    }                                                                       \
  }

/* Global contexts for LLVM compilation */
static LLVMModuleRef module;
static LLVMPassManagerRef pass_manager;
static LLVMExecutionEngineRef ex_engine;
static LLVMBuilderRef builder;
static Type llvm_u32;
static Type llvm_u64;
static Type llvm_u64_ptr;
static Type llvm_double;
static Type llvm_double_ptr;
static Type llvm_void_ptr;
static Type llvm_void_ptr_ptr;

static Value lvc_null;
static Value lvc_u32_one;
static Value lvc_data_mask;
static Value lvc_nil;

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
  LLVMInitializeFunctionPassManager(pass_manager);

  /* Builder and execution engine */
  char *errs;
  LLVMBool err = LLVMCreateJITCompilerForModule(&ex_engine, module, 100, &errs);
  xassert(!err);
  builder = LLVMCreateBuilder();
  xassert(builder != NULL);

  /* Useful types used in lots of places */
  llvm_u32          = LLVMInt32Type();
  llvm_u64          = LLVMInt64Type();
  llvm_u64_ptr      = LLVMPointerType(llvm_u64, 0);
  llvm_double       = LLVMDoubleType();
  llvm_double_ptr   = LLVMPointerType(llvm_double, 0);
  llvm_void_ptr     = LLVMPointerType(LLVMInt8Type(), 0);
  llvm_void_ptr_ptr = LLVMPointerType(llvm_void_ptr, 0);

  /* Constants */
  lvc_null      = LLVMConstNull(llvm_void_ptr);
  lvc_u32_one   = LLVMConstInt(llvm_u32, 1, FALSE);
  lvc_data_mask = LLVMConstInt(llvm_u64, LUAV_DATA_MASK, FALSE);
  lvc_nil       = LLVMConstInt(llvm_u64, LUAV_NIL, FALSE);

  /* Adding functions */
  Type lhash_get_args[2] = {llvm_void_ptr, llvm_u64};
  Type lhash_get_type = LLVMFunctionType(llvm_u64, lhash_get_args, 2, 0);
  LLVMAddFunction(module, "lhash_get", lhash_get_type);
  Type vm_fun_args[6] = {llvm_void_ptr, llvm_void_ptr, llvm_u32,
                                llvm_u32, llvm_u32, llvm_u32};
  Type vm_fun_type = LLVMFunctionType(llvm_u32, vm_fun_args, 6, 0);
  LLVMAddFunction(module, "vm_fun", vm_fun_type);
  Type memset_args[3] = {llvm_void_ptr, llvm_u32, llvm_u64};
  Type memset_type = LLVMFunctionType(llvm_void_ptr, memset_args, 3, 0);
  LLVMAddFunction(module, "memset", memset_type);
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
 * @brief Builds a "bail out"
 *
 * @param ret the value to return
 * @param num_regs the number of registers to restore
 * @param regs the registers to restore
 * @param base_addr the address which when dereferenced will point to the
 *        base of the lua stack.
 */
static void llvm_build_return(i32 ret, u32 num_regs, Value *regs,
                              Value base_addr) {
  u32 i;
  Value base = LLVMBuildLoad(builder, base_addr, "");
  for (i = 0; i < num_regs; i++) {
    Value off  = LLVMConstInt(llvm_u32, i, FALSE);
    Value addr = LLVMBuildInBoundsGEP(builder, base, &off, 1, "");
    Value val  = LLVMBuildLoad(builder, regs[i], "");
    LLVMBuildStore(builder, val, addr);
  }
  LLVMBuildRet(builder, LLVMConstInt(llvm_u32, (u32) ret, TRUE));
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
  Value bv, cv;
  if (B(code) >= 256) {
    bv = LLVMBuildBitCast(builder, consts[B(code) - 256], llvm_double, "");
  } else {
    bv = LLVMBuildLoad(builder, regs[B(code)], "");
    bv = LLVMBuildBitCast(builder, bv, llvm_double, "");
  }
  if (C(code) >= 256) {
    cv = LLVMBuildBitCast(builder, consts[C(code) - 256], llvm_double, "");
  } else {
    cv = LLVMBuildLoad(builder, regs[C(code)], "");
    cv = LLVMBuildBitCast(builder, cv, llvm_double, "");
  }
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
 *
 * @return the compiled function, which can be run by passing it over
 *         to llvm_run()
 */
jfunc_t* llvm_compile(lfunc_t *func, u32 start, u32 end) {
  LLVMBasicBlockRef blocks[func->num_instrs];
  Value regs[func->max_stack];
  Value consts[func->num_consts];
  char name[20];
  u32 i, j;
  Type params[2] = {llvm_void_ptr, LLVMPointerType(llvm_u32, 0)};

  Type  funtyp   = LLVMFunctionType(llvm_u32, params, 2, FALSE);
  Value function = LLVMAddFunction(module, "test", funtyp);
  Value closure  = LLVMGetParam(function, 0);
  Value jargs    = LLVMGetParam(function, 1);

  LLVMBasicBlockRef startbb = LLVMAppendBasicBlock(function, "start");
  LLVMPositionBuilderAtEnd(builder, startbb);
  /* Create the blocks and allocas */
  memset(blocks, 0, sizeof(blocks));
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

  /* Calculate stack base */
  Value base_addr = LLVMConstInt(llvm_u64, (size_t) &vm_stack->base, 0);
  Type base_typ   = LLVMPointerType(LLVMPointerType(llvm_u64, 0), 0);
  base_addr       = LLVMBuildIntToPtr(builder, base_addr, base_typ, "base");

  /* Copy the lua stack onto the C stack */
  Value lstack = get_stack_base(base_addr, stacki, "lstack");
  for (i = 0; i < func->max_stack; i++) {
    Value off  = LLVMConstInt(llvm_u32, i, 0);
    Value addr = LLVMBuildInBoundsGEP(builder, lstack, &off, 1, "");
    Value val  = LLVMBuildLoad(builder, addr, "");
    LLVMBuildStore(builder, val, regs[i]);
  }

  LLVMBuildBr(builder, blocks[start]);

  /* Translate! */
  for (i = start; i <= end;) {
    LLVMPositionBuilderAtEnd(builder, blocks[i]);
    u32 code = func->instrs[i++].instr;

    switch (OP(code)) {
      case OP_MOVE: {
        Value val = LLVMBuildLoad(builder, regs[B(code)], "mv");
        LLVMBuildStore(builder, val, regs[A(code)]);
        GOTOBB(i);
        break;
      }

      case OP_LOADK: {
        LLVMBuildStore(builder, consts[BX(code)], regs[A(code)]);
        GOTOBB(i);
        break;
      }

      case OP_LOADNIL: {
        for (j = A(code); j <= B(code); j++) {
          LLVMBuildStore(builder, lvc_nil, regs[j]);
        }
        GOTOBB(i);
        break;
      }

      /* TODO: assumes floats */
      case OP_ADD:
        build_binop(code, consts, regs, LLVMBuildFAdd);
        GOTOBB(i);
        break;
      case OP_SUB:
        build_binop(code, consts, regs, LLVMBuildFSub);
        GOTOBB(i);
        break;

      case OP_LT: {
        /* TODO: assumes floats */
        Value bv, cv;
        if (B(code) >= 256) {
          bv = LLVMBuildBitCast(builder, consts[B(code) - 256],
                               llvm_double, "lt_bf");
        } else {
          bv = LLVMBuildLoad(builder, regs[B(code)], "lt_b64");
          bv = LLVMBuildBitCast(builder, bv, llvm_double, "lt_bf");
        }
        if (C(code) >= 256) {
          cv = LLVMBuildBitCast(builder, consts[C(code) - 256],
                               llvm_double, "lt_cf");
        } else {
          cv = LLVMBuildLoad(builder, regs[C(code)], "lt_c64");
          cv = LLVMBuildBitCast(builder, cv, llvm_double, "lt_cf");
        }
        LLVMRealPredicate pred = A(code) ? LLVMRealUGE : LLVMRealULT;
        Value res = LLVMBuildFCmp(builder, pred, bv, cv, "lt");

        /* If a destination block is out of bounds, then create a new basic
           block which just returns the given integer of the basic block which
           is out of bounds. */
        LLVMBasicBlockRef truebb = blocks[i + 1];
        LLVMBasicBlockRef falsebb = blocks[i];
        if (i + 1 > end || i + 1 < start) {
          truebb = LLVMAppendBasicBlock(function, "retblock");
          LLVMPositionBuilderAtEnd(builder, truebb);
          llvm_build_return((i32) i + 1, func->max_stack, regs, base_addr);
        }
        if (i > end || i < start) {
          truebb = LLVMAppendBasicBlock(function, "retblock");
          LLVMPositionBuilderAtEnd(builder, truebb);
          llvm_build_return((i32) i, func->max_stack, regs, base_addr);
        }
        LLVMPositionBuilderAtEnd(builder, blocks[i - 1]);

        LLVMBuildCondBr(builder, res, truebb, falsebb);
        break;
      }

      case OP_JMP: {
        GOTOBB((u32) ((i32) i + SBX(code)));
        break;
      }

      case OP_RETURN: {
        /* TODO: variable numer of returns */
        Value ret_stack = get_stack_base(base_addr, retvi, "retstack");
        if (B(code) == 0) { return NULL; }

        /* Create actual return first, so everything can jump to it */
        LLVMBasicBlockRef endbb = LLVMAppendBasicBlock(function, "end");
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
          LLVMBasicBlockRef curbb = LLVMAppendBasicBlock(function, "ret");
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
        Value args[] = {
          closure_env,
          consts[BX(code)]
        };
        Value val = LLVMBuildCall(builder, fn, args, 2, "");
        LLVMBuildStore(builder, val, regs[A(code)]);
        GOTOBB(i);
        break;
      }

      case OP_CALL: {
        /* TODO: varargs, multiple returns, etc... */
        if (B(code) == 0 || C(code) == 0) { return NULL; }
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
        Value closure = LLVMBuildLoad(builder, regs[A(code)], "f");
        closure = LLVMBuildAnd(builder, closure, lvc_data_mask, "");
        closure = LLVMBuildIntToPtr(builder, closure, llvm_void_ptr, "");

        // call the function
        Value av = LLVMConstInt(llvm_u32, A(code), FALSE);
        Value fn = LLVMGetNamedFunction(module, "vm_fun");
        xassert(fn != NULL);
        Value args[] = {
          closure,
          lvc_null,
          LLVMConstInt(llvm_u32, num_args, FALSE),
          LLVMBuildAdd(builder, stacki, LLVMConstAdd(av, lvc_u32_one), ""),
          LLVMConstInt(llvm_u32, num_rets, FALSE),
          LLVMBuildAdd(builder, stacki, av, "")
        };
        /*Value ret =*/ LLVMBuildCall(builder, fn, args, 6, "");
        // nilify unused return parameters
        /* TODO
        Value memset_fn = LLVMGetNamedFunction(module, "memset");
        Value memset_argvs = {
          //
        };
        LLVMBuildCall(builder, memset_fn, memset_argvs, 3, "memset");
        */

        // copy return values from lua stack back to c stack
        stack = get_stack_base(base_addr, stacki, "");
        for (j = a; j < a + num_rets; j++) {
          Value off  = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, &off, 1, "");
          Value val  = LLVMBuildLoad(builder, addr, "");
          LLVMBuildStore(builder, val, regs[j]);
        }
        GOTOBB(i);
        break;
      }

      default: {
        // TODO cleanup
        return NULL;
      }
    }
  }

  LLVMRunFunctionPassManager(pass_manager, function);
  //LLVMDumpModule(module);
  return LLVMGetPointerToGlobal(ex_engine, function);
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
  // jitf *f = LLVMGetPointerToGlobal(ex_engine, function);
  jitf *f = function;
  return f(closure, args);
}
