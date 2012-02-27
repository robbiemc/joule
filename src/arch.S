#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a ## b
#ifndef __USER_LABEL_PREFIX__
#define __USER_LABEL_PREFIX__
#endif

#define ENTRY(n) .globl CONCAT1(__USER_LABEL_PREFIX__, n);\
                 CONCAT1(__USER_LABEL_PREFIX__, n) ##:

/**
 * @brief Save caller-saved registers into a structure so that they may later
 *        be jumped back to.
 *
 * This function behaves like setjmp, where it returns twice. The first return
 * returns 0, while the second return returns nonzero.
 *
 * The reason we don't use setjmp/longjmp is that for garbage collection, we
 * might need to alter the caller-saved registers. It's difficult to peek
 * inside of a jmpbuf, so we create our own version of these functions where
 * we know the structure of the buffer in which everything is saved.
 *
 * @param regs the struct in which to save the registers and state
 */
ENTRY(arch_save_callee)
  /* regs = %rdi */
  mov %rbp, (%rdi)
  mov %rbx, 8(%rdi)
  mov %r12, 16(%rdi)
  mov %r13, 24(%rdi)
  mov %r14, 32(%rdi)
  mov %r15, 40(%rdi)
  mov (%rsp), %rax   /* return address */
  mov %rax, 48(%rdi)
  mov %rsp, %rax
  add $8, %rax       /* stack pointer of our calling function is rsp+8 */
  mov %rax, 56(%rdi)
  mov $0, %rax
  ret

/**
 * @brief Save caller-saved registers into a structure so that they may later
 *        be jumped back to.
 *
 * This function behaves like setjmp, where it returns twice. The first return
 * returns 0, while the second return returns nonzero.
 *
 * The reason we don't use setjmp/longjmp is that for garbage collection, we
 * might need to alter the caller-saved registers. It's difficult to peek
 * inside of a jmpbuf, so we create our own version of these functions where
 * we know the structure of the buffer in which everything is saved.
 *
 * @param regs the struct in which to save the registers and state
 */
ENTRY(arch_assume_callee)
  /* regs = %rdi */
  mov (%rdi), %rbp
  mov 8(%rdi), %rbx
  mov 16(%rdi), %r12
  mov 24(%rdi), %r13
  mov 32(%rdi), %r14
  mov 40(%rdi), %r15
  mov 56(%rdi), %rsp
  mov 48(%rdi), %rcx /* return address */
  mov $1, %rax
  jmp *%rcx

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
