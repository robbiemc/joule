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
}
