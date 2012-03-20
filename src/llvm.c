/**
 * @file llvm.c
 * @brief Will eventually contain JIT-related compliation tied into LLVM
 */

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Transforms/Scalar.h>
#include <stddef.h>

#include "config.h"
#include "lhash.h"
#include "opcode.h"
#include "panic.h"
#include "vm.h"

/* Global contexts for LLVM compilation */
static LLVMModuleRef module;
static LLVMPassManagerRef pass_manager;
static LLVMExecutionEngineRef ex_engine;
static LLVMBuilderRef builder;
static LLVMTypeRef llvm_u32;
static LLVMTypeRef llvm_u64;
static LLVMTypeRef llvm_u64_ptr;
static LLVMTypeRef llvm_double;
static LLVMTypeRef llvm_double_ptr;
static LLVMTypeRef llvm_void_ptr;
static LLVMTypeRef llvm_void_ptr_ptr;

static LLVMValueRef lvc_null;
static LLVMValueRef lvc_u32_one;
static LLVMValueRef lvc_data_mask;
static LLVMValueRef lvc_nil;

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

  /* Builder and execution engine */
  char *err;
  LLVMBool ret = LLVMCreateJITCompilerForModule(&ex_engine, module, 100, &err);
  xassert(!ret);
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
  LLVMTypeRef lhash_get_args[2] = {llvm_void_ptr, llvm_u64};
  LLVMTypeRef lhash_get_type = LLVMFunctionType(llvm_u64, lhash_get_args, 2, 0);
  LLVMAddFunction(module, "lhash_get", lhash_get_type);
  LLVMTypeRef vm_fun_args[6] = {llvm_void_ptr, llvm_void_ptr, llvm_u32,
                                llvm_u32, llvm_u32, llvm_u32};
  LLVMTypeRef vm_fun_type = LLVMFunctionType(llvm_u32, vm_fun_args, 6, 0);
  LLVMAddFunction(module, "vm_fun", vm_fun_type);
  LLVMTypeRef memset_args[3] = {llvm_void_ptr, llvm_u32, llvm_u64};
  LLVMTypeRef memset_type = LLVMFunctionType(llvm_void_ptr, memset_args, 3, 0);
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

void llvm_munge(lfunc_t *func) {
  LLVMBasicBlockRef blocks[func->num_instrs];
  LLVMValueRef regs[func->max_stack];
  LLVMValueRef consts[func->num_consts];
  char name[20];
  u32 i, j;
  LLVMValueRef indices[2] = {LLVMConstInt(llvm_u64, 0, 0), NULL};
  LLVMTypeRef params[] = { llvm_void_ptr, llvm_u32 };

  LLVMTypeRef  funtyp   = LLVMFunctionType(LLVMVoidType(), params, 2, FALSE);
  LLVMValueRef function = LLVMAddFunction(module, "test", funtyp);
  LLVMValueRef closure  = LLVMGetParam(function, 0);
  LLVMValueRef stacki   = LLVMGetParam(function, 1);

  /* Create the blocks and allocas */
  for (i = 0; i < func->num_instrs; i++) {
    blocks[i] = LLVMAppendBasicBlock(function, "block");
  }
  LLVMPositionBuilderAtEnd(builder, blocks[0]);
  for (i = 0; i < func->num_consts; i++) {
    consts[i] = LLVMConstInt(llvm_u64, func->consts[i], FALSE);
  }
  for (i = 0; i < func->max_stack; i++) {
    sprintf(name, "reg%d", i);
    regs[i] = LLVMBuildAlloca(builder, llvm_u64, name);
  }

  /* Calculate closure->env */
  LLVMValueRef offset = LLVMConstInt(llvm_u64, offsetof(lclosure_t, env), 0);
  LLVMValueRef env_addr = LLVMBuildInBoundsGEP(builder, closure, &offset, 1,"");
  env_addr = LLVMBuildBitCast(builder, env_addr, llvm_void_ptr_ptr, "");
  LLVMValueRef closure_env = LLVMBuildLoad(builder, env_addr, "env");

  /* Calculate stack base */
  LLVMValueRef base_addr = LLVMConstInt(llvm_u64, (size_t) &vm_stack->base, 0);
  LLVMTypeRef base_typ  = LLVMPointerType(LLVMPointerType(LLVMArrayType(llvm_u64, 2), 0), 0);
  base_addr = LLVMBuildIntToPtr(builder, base_addr, base_typ, "");
  LLVMValueRef stack = LLVMBuildLoad(builder, base_addr, "");
  stack = LLVMBuildInBoundsGEP(builder, stack, &stacki, 1, "stack");

  /* Copy the lua stack onto the C stack */
  for (i = 0; i < func->max_stack; i++) {
    indices[1] = LLVMConstInt(llvm_u64, i, 0);
    LLVMValueRef addr = LLVMBuildInBoundsGEP(builder, stack, indices, 2, "");
    LLVMValueRef val  = LLVMBuildLoad(builder, addr, "");
    LLVMBuildStore(builder, val, regs[i]);
  }

  /* Translate! */
  for (i = 0; i < func->num_instrs;) {
    LLVMPositionBuilderAtEnd(builder, blocks[i]);
    u32 code = func->instrs[i++];

    switch (OP(code)) {
      case OP_MOVE: {
        LLVMValueRef val = LLVMBuildLoad(builder, regs[B(code)], "mv");
        LLVMBuildStore(builder, val, regs[A(code)]);

        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      case OP_LOADK: {
        LLVMBuildStore(builder, consts[BX(code)], regs[A(code)]);
        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      case OP_LOADNIL: {
        for (j = A(code); j <= B(code); j++) {
          LLVMBuildStore(builder, lvc_nil, regs[j]);
        }
        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      case OP_ADD: {
        /* TODO: assumes floats */
        LLVMValueRef bv, cv;
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
        LLVMValueRef res = LLVMBuildFAdd(builder, bv, cv, "add_resf");
        res = LLVMBuildBitCast(builder, res, llvm_u64, "add_res64");
        LLVMBuildStore(builder, res, regs[A(code)]);

        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      case OP_LT: {
        /* TODO: assumes floats */
        LLVMValueRef bv, cv;
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
        LLVMValueRef res = LLVMBuildFCmp(builder, pred, bv, cv, "lt");
        LLVMBuildCondBr(builder, res, blocks[i + 1], blocks[i]);
        break;
      }

      case OP_JMP: {
        LLVMBuildBr(builder, blocks[(i32) i + SBX(code)]);
        break;
      }

      case OP_RETURN: {
        LLVMBuildRetVoid(builder);
        break;
      }

      case OP_GETGLOBAL: {
        /* TODO: metatable? */
        LLVMValueRef fn = LLVMGetNamedFunction(module, "lhash_get");
        xassert(fn != NULL);
        LLVMValueRef args[] = {
          closure_env,
          consts[BX(code)]
        };
        LLVMValueRef val = LLVMBuildCall(builder, fn, args, 2, "");
        LLVMBuildStore(builder, val, regs[A(code)]);
        LLVMBuildBr(builder, blocks[i]);
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
          LLVMValueRef addr = LLVMBuildInBoundsGEP(builder, stack, indices, 2, "");
          LLVMValueRef val  = LLVMBuildLoad(builder, regs[j], "");
          LLVMBuildStore(builder, val, addr);
        }

        // get the function pointer
        LLVMValueRef closure = LLVMBuildLoad(builder, regs[A(code)], "f");
        closure = LLVMBuildAnd(builder, closure, lvc_data_mask, "");
        closure = LLVMBuildIntToPtr(builder, closure, llvm_void_ptr, "");

        // call the function
        LLVMValueRef av = LLVMConstInt(llvm_u32, A(code), FALSE);
        LLVMValueRef fn = LLVMGetNamedFunction(module, "vm_fun");
        xassert(fn != NULL);
        LLVMValueRef args[] = {
          closure,
          lvc_null,
          LLVMConstInt(llvm_u32, num_args, FALSE),
          LLVMBuildAdd(builder, stacki, LLVMConstAdd(av, lvc_u32_one), ""),
          LLVMConstInt(llvm_u32, num_rets, FALSE),
          LLVMBuildAdd(builder, stacki, av, "")
        };
        /*LLVMValueRef ret =*/ LLVMBuildCall(builder, fn, args, 6, "");
        // nilify unused return parameters
        /* TODO
        LLVMValueRef memset_fn = LLVMGetNamedFunction(module, "memset");
        LLVMValueRef memset_argvs = {
          //
        };
        LLVMBuildCall(builder, memset_fn, memset_argvs, 3, "memset");
        */

        // copy things from lua stack back to c stack
        for (j = a; j < a + num_rets; j++) {
          indices[1] = LLVMConstInt(llvm_u64, j, 0);
          LLVMValueRef addr = LLVMBuildInBoundsGEP(builder, stack, indices, 2, "");
          LLVMValueRef val  = LLVMBuildLoad(builder, addr, "");
          LLVMBuildStore(builder, val, regs[j]);
        }
        LLVMBuildBr(builder, blocks[i]);
        break;
      }

      default: {
        LLVMBuildBr(builder, blocks[i]);
        break;
      }
    }
  }

  LLVMRunFunctionPassManager(pass_manager, function);
  LLVMDumpModule(module);

  u32 lstacki = vm_stack_alloc(vm_stack, 100);
  static lclosure_t lclosure;
  lclosure.type = LUAF_LUA;
  lclosure.function.lua = func;
  lclosure.env = lua_globals;
  LLVMGenericValueRef args[2] = {
    LLVMCreateGenericValueOfPointer(&lclosure),
    LLVMCreateGenericValueOfInt(llvm_u32, lstacki, FALSE)
  };
  LLVMGenericValueRef ret = LLVMRunFunction(ex_engine, function, 2, args);
  LLVMDisposeGenericValue(args[0]);
  LLVMDisposeGenericValue(args[1]);
  LLVMDisposeGenericValue(ret);
}
