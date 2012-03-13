/**
 * @file llvm.c
 * @brief Will eventually contain JIT-related compliation tied into LLVM
 */

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Transforms/Scalar.h>

#include "config.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

/* Global contexts for LLVM compilation */
static LLVMModuleRef module;
static LLVMPassManagerRef pass_manager;
static LLVMExecutionEngineRef ex_engine;
static LLVMBuilderRef builder;
static LLVMTypeRef llvm_u64;
static LLVMTypeRef llvm_double;
static LLVMTypeRef llvm_double_ptr;

/**
 * @brief Initialize LLVM globals and engines needed for JIT compilation
 */
INIT static void llvm_init() {
  module = LLVMModuleCreateWithName("joule");
  xassert(module != NULL);

  pass_manager = LLVMCreateFunctionPassManagerForModule(module);
  xassert(pass_manager != NULL);

  LLVMAddVerifierPass(pass_manager);
  LLVMAddCFGSimplificationPass(pass_manager);
  LLVMAddPromoteMemoryToRegisterPass(pass_manager);

  char *err;
  LLVMBool ret = LLVMCreateJITCompilerForModule(&ex_engine, module, 100, &err);
  xassert(ret);

  builder = LLVMCreateBuilder();
  xassert(builder != NULL);

  llvm_u64 = LLVMInt64Type();
  llvm_double = LLVMDoubleType();
  llvm_double_ptr = LLVMPointerType(llvm_double, 0);
}

/**
 * @brief Deallocates all memory associated with LLVM allocated on startup
 */
DESTROY static void llvm_destroy() {
  LLVMDisposeExecutionEngine(ex_engine);
  LLVMDisposePassManager(pass_manager);
  LLVMDisposeModule(module);
}

void llvm_munge(lfunc_t *func) {
  LLVMBasicBlockRef blocks[func->num_instrs];
  LLVMValueRef regs[func->max_stack];
  LLVMValueRef consts[func->num_consts];
  char name[20];
  u32 i;

  LLVMTypeRef funtyp = LLVMFunctionType(LLVMVoidType(), NULL, 0, FALSE);
  LLVMValueRef function = LLVMAddFunction(module, "test", funtyp);

  for (i = 0; i < func->num_instrs; i++) {
    blocks[i] = LLVMAppendBasicBlock(function, "block");
  }

  LLVMPositionBuilderAtEnd(builder, blocks[0]);
  for (i = 0; i < func->max_stack; i++) {
    sprintf(name, "reg%d", i);
    regs[i] = LLVMBuildAlloca(builder, llvm_u64, name);
  }
  for (i = 0; i < func->num_consts; i++) {
    consts[i] = LLVMConstInt(llvm_u64, func->consts[i], FALSE);
  }

  for (i = 0; i < func->num_instrs;) {
    LLVMPositionBuilderAtEnd(builder, blocks[i]);
    u32 code = func->instrs[i++];

    switch (OP(code)) {
      case OP_MOVE: {
        LLVMValueRef val = LLVMBuildLoad(builder, regs[B(code)], "move_load");
        LLVMBuildStore(builder, val, regs[A(code)]);

        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      case OP_LOADK: {
        LLVMBuildStore(builder, consts[BX(code)], regs[A(code)]);
        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      case OP_ADD: {
        /* TODO: assumes floats */
        LLVMValueRef bv, cv;
        if (B(code) >= 256) {
          bv = LLVMBuildUIToFP(builder, consts[B(code) - 256],
                               llvm_double, "add_bf");
        } else {
          LLVMValueRef ptr = LLVMBuildPointerCast(builder, regs[B(code)],
                                                  llvm_double_ptr, "add_bloc");
          bv = LLVMBuildLoad(builder, ptr, "add_bf");
        }
        if (C(code) >= 256) {
          cv = LLVMBuildUIToFP(builder, consts[C(code) - 256],
                               llvm_double, "add_cf");
        } else {
          LLVMValueRef ptr = LLVMBuildPointerCast(builder, regs[C(code)],
                                                  llvm_double_ptr, "add_cloc");
          cv = LLVMBuildLoad(builder, ptr, "add_cf");
        }
        LLVMValueRef res = LLVMBuildFAdd(builder, bv, cv, "add");
        LLVMValueRef ptr = LLVMBuildPointerCast(builder, regs[A(code)],
                                                llvm_double_ptr, "add_dst");
        LLVMBuildStore(builder, res, ptr);

        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      case OP_LT: {
        /* TODO: assumes floats */
        LLVMValueRef bv, cv;
        if (B(code) >= 256) {
          bv = LLVMBuildUIToFP(builder, consts[B(code) - 256],
                               llvm_double, "add_bf");
        } else {
          LLVMValueRef ptr = LLVMBuildPointerCast(builder, regs[B(code)],
                                                  llvm_double_ptr, "add_bloc");
          bv = LLVMBuildLoad(builder, ptr, "add_bf");
        }
        if (C(code) >= 256) {
          cv = LLVMBuildUIToFP(builder, consts[C(code) - 256],
                               llvm_double, "add_cf");
        } else {
          LLVMValueRef ptr = LLVMBuildPointerCast(builder, regs[C(code)],
                                                  llvm_double_ptr, "add_cloc");
          cv = LLVMBuildLoad(builder, ptr, "add_cf");
        }
        LLVMRealPredicate pred = A(code) ? LLVMRealOGE : LLVMRealOLT;
        LLVMValueRef res = LLVMBuildFCmp(builder, pred, bv, cv, "lt");
        LLVMBuildCondBr(builder, res, blocks[i + 1], blocks[i]);
        break;
      }

      case OP_JMP: {
        LLVMBuildBr(builder, blocks[(i32) i + SBX(code)]);
        break;
      }

      default: {
        if (i == func->num_instrs) {
          LLVMBuildRetVoid(builder);
        } else {
          LLVMBuildBr(builder, blocks[i]);
        }
        break;
      }
    }
  }

  LLVMRunFunctionPassManager(pass_manager, function);
  LLVMDumpValue(function);
}
