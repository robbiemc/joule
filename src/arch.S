#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a ## b
#ifndef __USER_LABEL_PREFIX__
#define __USER_LABEL_PREFIX__
#endif

#define ENTRY(n) .globl CONCAT1(__USER_LABEL_PREFIX__, n);\
                 CONCAT1(__USER_LABEL_PREFIX__, n) ##:

#ifdef __x86_64

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

#else

/* 4(%esp) = where to save %esp, 8(%esp) = new %esp */
ENTRY(arch_coroutine_swap)
  /* Save callee-saved registers */
  push %ebp
  push %ebx
  push %edi
  push %esi

  /* Change contexts */
  mov 20(%esp), %eax
  mov %esp, (%eax)
  mov 24(%esp), %esp

  /* Restore registers */
  pop %esi
  pop %edi
  pop %ebx
  pop %ebp

  ret

#endif
