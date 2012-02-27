#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a ## b
#ifndef __USER_LABEL_PREFIX__
#define __USER_LABEL_PREFIX__
#endif

#define EXT(n) CONCAT1(__USER_LABEL_PREFIX__, n)
#define ENTRY(n) .globl CONCAT1(__USER_LABEL_PREFIX__, n);\
                 CONCAT1(__USER_LABEL_PREFIX__, n) ##:

/**
 * @brief Assembly wrapper for garbage collection, delegates to the internal
 *        GC method
 *
 * This is needed to ensure that all caller-saved registers are on the stack so
 * that if they contain pointers, they can be modified. Also, this gives the
 * lowest stack pointer to the internal GC method so that it knows where to
 * start when rewriting the stack.
 */
ENTRY(garbage_collect)
  subq  $8, %rsp
  pushq %rbp
  pushq %rbx
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15
  movq  %rsp, %rdi
  call  EXT(gc_internal)
  popq  %r15
  popq  %r14
  popq  %r13
  popq  %r12
  popq  %rbx
  popq  %rbp
  addq  $8, %rsp
  ret

/* %rdi = where to save %rsp, %rsi = new %rsp */
ENTRY(arch_coroutine_swap)
  /* Save callee-saved registers */
  push %rbp
  push %rbx
  push %r12
  push %r13
  push %r14
  push %r15

  /* Change contexts */
  mov %rsp, (%rdi)
  mov %rsi, %rsp

  /* Restore registers */
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rbx
  pop %rbp

  ret
