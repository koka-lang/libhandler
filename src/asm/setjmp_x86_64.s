/* ----------------------------------------------------------------------------
  Copyright (c) 2016, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

/*
Code for x86-64 calling convention: Win64 (clang, mingw)
See: https://en.wikipedia.org/wiki/X86_calling_conventions

jump_buf layout (compatible with msvc):
   0: rdx ( frame pointer on msvc)
   8: rbx
  16: rsp
  24: rbp
  32: rsi
  40: rdi
  48: r12
  56: r13
  64: r14
  72: r15
  80: rip
  88: sse control word
  92: fpu control word
  94: unused
  96: xmm6
  ... (128-bit registers)
 240: xmm15 
 256: sizeof jmp_buf
*/

.global _lh_setjmp
.global _lh_longjmp

_lh_setjmp:                 /* input: rcx: jmp_buf, rdx: frame pointer */
  movq    (%rsp), %rax      /* return address is on the stack */
  movq    %rax, 80 (%rcx)   /* rip */

  leaq    8 (%rsp), %rax  
  movq    %rax, 16 (%rcx)   /* rsp: just from before the return address */

  movq    %rdx,  0 (%rcx)   /* save registers */
  movq    %rbx,  8 (%rcx)
  movq    %rbp, 24 (%rcx) 
  movq    %rsi, 32 (%rcx) 
  movq    %rdi, 40 (%rcx) 
  movq    %r12, 48 (%rcx) 
  movq    %r13, 56 (%rcx) 
  movq    %r14, 64 (%rcx) 
  movq    %r15, 72 (%rcx)

  stmxcsr 88 (%rcx)          /* save sse control word */
  fnstcw  92 (%rcx)          /* save fpu control word */

  movdqa  %xmm6,   96 (%rcx) /* save sse registers */
  movdqa  %xmm7,  112 (%rcx) 
  movdqa  %xmm8,  128 (%rcx) 
  movdqa  %xmm9,  144 (%rcx) 
  movdqa  %xmm10, 160 (%rcx) 
  movdqa  %xmm11, 176 (%rcx) 
  movdqa  %xmm12, 192 (%rcx) 
  movdqa  %xmm13, 208 (%rcx) 
  movdqa  %xmm14, 224 (%rcx) 
  movdqa  %xmm15, 240 (%rcx) 

  xor     %rax, %rax          /* return 0 */
  ret

_lh_longjmp:                  /* rcx: jmp_buf, edx: arg */
  movq  %rdx, %rax            /* return arg to rax */
  
  movq   0 (%rcx), %rdx       /* restore registers */
  movq   8 (%rcx), %rbx
  movq  24 (%rcx), %rbp
  movq  32 (%rcx), %rsi
  movq  40 (%rcx), %rdi
  movq  48 (%rcx), %r12
  movq  56 (%rcx), %r13
  movq  64 (%rcx), %r14
  movq  72 (%rcx), %r15

  ldmxcsr 88 (%rcx)           /* restore sse control word */
  fnclex                      /* clear fpu exception flags */
  fldcw   92 (%rcx)           /* restore fpu control word */
  
  movdqa   96 (%rcx), %xmm6   /* restore sse registers */
  movdqa  112 (%rcx), %xmm7
  movdqa  128 (%rcx), %xmm8 
  movdqa  144 (%rcx), %xmm9 
  movdqa  160 (%rcx), %xmm10 
  movdqa  176 (%rcx), %xmm11 
  movdqa  192 (%rcx), %xmm12 
  movdqa  208 (%rcx), %xmm13 
  movdqa  224 (%rcx), %xmm14
  movdqa  240 (%rcx), %xmm15

  testl %eax, %eax            /* longjmp should never return 0 */ 
  jnz   ok
  incl  %eax
ok:
  movq  16 (%rcx), %rsp        /* set the stack frame */     
  jmpq *80 (%rcx)              /* and jump to rip */
