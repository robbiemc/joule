/**
 * @file llvm.c
 * @brief Will eventually contain JIT-related compliation tied into LLVM
 */

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Transforms/Scalar.h>

#include "config.h"
#include "panic.h"
#include "vm.h"

/* Global contexts for LLVM compilation */
static LLVMModuleRef module;
static LLVMPassManagerRef pass_manager;
static LLVMExecutionEngineRef ex_engine;
static LLVMBuilderRef builder;
static LLVMTypeRef llvm_u64;

/**
 * @brief Initialize LLVM globals and engines needed for JIT compilation
 */
INIT static void llvm_init() {
  module = LLVMModuleCreateWithName("joule");
  xassert(module != NULL);

  pass_manager = LLVMCreatePassManager();
  xassert(pass_manager != NULL);

  LLVMAddCFGSimplificationPass(pass_manager);
  LLVMAddMemCpyOptPass(pass_manager);
  LLVMAddConstantPropagationPass(pass_manager);
  LLVMAddDemoteMemoryToRegisterPass(pass_manager);

  char *err;
  LLVMBool ret = LLVMCreateJITCompilerForModule(&ex_engine, module, 3, &err);
  xassert(ret);

  builder = LLVMCreateBuilder();
  xassert(builder != NULL);

  llvm_u64 = LLVMInt64Type();
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
  size_t i;

  for (i = 0; i < func->num_instrs; i++) {
    blocks[i] = LLVMGetInsertBlock(builder);
  }
  for (i = 0; i < func->max_stack; i++) {
    regs[i] = LLVMBuildAlloca(builder, llvm_u64, "reg");
  }
  for (i = 0; i < func->num_consts; i++) {
    consts[i] = LLVMConstInt(llvm_u64, func->consts[i], FALSE);
  }
}
