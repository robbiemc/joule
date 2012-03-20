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
  LLVMAddPromoteMemoryToRegisterPass(pass_manager);
  LLVMAddGVNPass(pass_manager);
  LLVMAddLoopRotatePass(pass_manager);

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
  LLVMDisposePassManager(pass_manager);
  LLVMDisposeBuilder(builder);
  LLVMDisposeExecutionEngine(ex_engine);
}

static void llvm_build_return(i32 ret, u32 num_regs, Value *regs,
                              Value stack_base_addr) {
  u32 i;
  Value indices[2] = {LLVMConstInt(llvm_u32, 0, FALSE), NULL};
  Value stack_base = LLVMBuildLoad(builder, stack_base_addr, "");
  for (i = 0; i < num_regs; i++) {
    indices[1] = LLVMConstInt(llvm_u32, i, FALSE);
    Value addr = LLVMBuildInBoundsGEP(builder, stack_base, indices, 2, "");
    Value val  = LLVMBuildLoad(builder, regs[i], "");
    LLVMBuildStore(builder, val, addr);
  }
  LLVMBuildRet(builder, LLVMConstInt(llvm_u32, (u32) ret, TRUE));
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
  Value indices[2] = {LLVMConstInt(llvm_u32, 0, 0), NULL};
  Type params[2] = {
    llvm_void_ptr,
    LLVMPointerType(LLVMArrayType(llvm_u32, JARGS), 0)
  };

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
    sprintf(name, "reg%d", i);
    regs[i] = LLVMBuildAlloca(builder, llvm_u64, name);
  }

  /* Calculate stacki, and LSTATE */
  indices[1] = LLVMConstInt(llvm_u32, JSTACKI, FALSE);
  Value stackia  = LLVMBuildInBoundsGEP(builder, jargs, indices, 2, "");
  Value stacki   = LLVMBuildLoad(builder, stackia, "");

  /* Calculate closure->env */
  Value offset = LLVMConstInt(llvm_u64, offsetof(lclosure_t, env), 0);
  Value env_addr = LLVMBuildInBoundsGEP(builder, closure, &offset, 1,"");
  env_addr = LLVMBuildBitCast(builder, env_addr, llvm_void_ptr_ptr, "");
  Value closure_env = LLVMBuildLoad(builder, env_addr, "env");

  /* Calculate stack base */
  Value base_addr = LLVMConstInt(llvm_u64, (size_t) &vm_stack->base, 0);
  Type arr_typ   = LLVMArrayType(llvm_u64, func->max_stack);
  Type base_typ  = LLVMPointerType(LLVMPointerType(arr_typ, 0), 0);
  base_addr = LLVMBuildIntToPtr(builder, base_addr, base_typ, "");
  Value stack = LLVMBuildLoad(builder, base_addr, "");
  stack = LLVMBuildInBoundsGEP(builder, stack, &stacki, 1, "stack");

  /* Copy the lua stack onto the C stack */
  for (i = 0; i < func->max_stack; i++) {
    indices[1] = LLVMConstInt(llvm_u64, i, 0);
    Value addr = LLVMBuildInBoundsGEP(builder, stack, indices, 2, "");
    Value val  = LLVMBuildLoad(builder, addr, "");
    LLVMBuildStore(builder, val, regs[i]);
  }

  LLVMBuildBr(builder, blocks[start]);

  /* Translate! */
  for (i = start; i <= end;) {
    LLVMPositionBuilderAtEnd(builder, blocks[i]);
    u32 code = func->instrs[i++];

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

      case OP_ADD: {
        /* TODO: assumes floats */
        Value bv, cv;
        if (B(code) >= 256) {
          bv = LLVMBuildBitCast(builder, consts[B(code) - 256],
                               llvm_double, "add_bf");
        } else {
          bv = LLVMBuildLoad(builder, regs[B(code)], "add_b64");
          bv = LLVMBuildBitCast(builder, bv, llvm_double, "add_bf");
        }
        if (C(code) >= 256) {
          cv = LLVMBuildBitCast(builder, consts[C(code) - 256],
                               llvm_double, "add_cf");
        } else {
          cv = LLVMBuildLoad(builder, regs[C(code)], "add_c64");
          cv = LLVMBuildBitCast(builder, cv, llvm_double, "add_cf");
        }
        Value res = LLVMBuildFAdd(builder, bv, cv, "add_resf");
        res = LLVMBuildBitCast(builder, res, llvm_u64, "add_res64");
        LLVMBuildStore(builder, res, regs[A(code)]);

        GOTOBB(i);
        break;
      }

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
        /* TODO: specify number of return values actually */
        /* TODO: put return values into return locations */
        LLVMBuildRet(builder, LLVMConstAllOnes(llvm_u32));
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
        xassert(B(code) > 0 && C(code) > 0);
        u32 num_args = B(code) - 1;
        u32 num_rets = C(code) - 1;
        // copy things from c stack to lua stack
        u32 a = A(code);
        for (j = a + 1; j < a + 1 + num_args; j++) {
          indices[1] = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, indices, 2, "");
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

        // copy things from lua stack back to c stack
        for (j = a; j < a + num_rets; j++) {
          indices[1] = LLVMConstInt(llvm_u64, j, 0);
          Value addr = LLVMBuildInBoundsGEP(builder, stack, indices, 2, "");
          Value val  = LLVMBuildLoad(builder, addr, "");
          LLVMBuildStore(builder, val, regs[j]);
        }
        GOTOBB(i);
        break;
      }

      default: {
        GOTOBB(i);
        break;
      }
    }
  }

  printf("%p %p\n", blocks[start], LLVMGetEntryBasicBlock(function));
  printf("%p\n", LLVMGetPreviousBasicBlock(blocks[start]));
  LLVMRunFunctionPassManager(pass_manager, function);
  LLVMDumpModule(module);
  return function;
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
  LLVMGenericValueRef jargs[2] = {
    LLVMCreateGenericValueOfPointer(closure),
    LLVMCreateGenericValueOfPointer(args)
  };
  LLVMGenericValueRef val = LLVMRunFunction(ex_engine, function, 2, jargs);
  LLVMDisposeGenericValue(jargs[0]);
  LLVMDisposeGenericValue(jargs[1]);
  i32 ret = (i32) LLVMGenericValueToInt(val, TRUE);
  LLVMDisposeGenericValue(val);
  return ret;
}
