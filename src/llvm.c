#include <llvm-c/Core.h>
#include <llvm-c/Transforms/Scalar.h>

void llvm_compile() {
  LLVMPassManagerRef pass = LLVMCreatePassManager();
  LLVMAddCFGSimplificationPass(pass);
}
