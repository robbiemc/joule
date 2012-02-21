#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a ## b
#ifndef __USER_LABEL_PREFIX__
#define __USER_LABEL_PREFIX__
#endif

#define ENTRY(n) .globl CONCAT1(__USER_LABEL_PREFIX__, n);\
                 CONCAT1(__USER_LABEL_PREFIX__, n) ##:

/* %rdi = where to save regs */
ENTRY(save_callee_regs)
  movq %rbp, (%rdi)
  movq %rbx, 8(%rdi)
  movq %r12, 16(%rdi)
  movq %r13, 24(%rdi)
  movq %r14, 32(%rdi)
  movq %r15, 40(%rdi)
  ret

/* %rdi = where to restore regs from */
ENTRY(assume_callee_regs)
  movq (%rdi), %rbp
  movq 8(%rdi), %rbx
  movq 16(%rdi), %r12
  movq 24(%rdi), %r13
  movq 32(%rdi), %r14
  movq 40(%rdi), %r15
  ret

/* %rdi = where to save %rsp, %rsi = new %rsp */
ENTRY(coroutine_swap_asm)
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
