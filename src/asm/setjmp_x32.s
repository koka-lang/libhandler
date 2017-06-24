/* ----------------------------------------------------------------------------
  Copyright (c) 2016, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

/*
*UNTESTED*
Code for x32 calling convention: Linux
See: https://en.wikipedia.org/wiki/X32_ABI

jump_buf layout 
   0: eip  (32 bits)
   4: unused (32 bits)
   8: rbx
  16: rsp 
  24: rbp
  32: r12
  40: r13
  48: r14
  56: r15
  64: fpu control word (16 bits)
  66: unused
  68: sse control word (32 bits)
  72: sizeof jmp_buf
*/



.global _lh_setjmp
.global _lh_longjmp

/* int _lh_setjmp(jmp_buf) 
  edi: jmp_buf
  esp: return address (32 bit)
*/
_lh_setjmp:                 /* rdi: jmp_buf */
  xorl    %rax, %rax
  movl    (%esp), %eax      /* eip: return address is on the stack */
  movq    %rax, 0 (%edi)    

  leaq    4 (%rsp), %rax    /* rsp - return address */
  movq    %rax, 16 (%edi)   

  movq    %rbx,  8 (%edi)   /* save registers */
  movq    %rbp, 24 (%edi) 
  movq    %r12, 32 (%edi) 
  movq    %r13, 40 (%edi) 
  movq    %r14, 48 (%edi) 
  movq    %r15, 56 (%edi) 

  fnstcw  64 (%edi)          /* save fpu control word */
  stmxcsr 68 (%edi)          /* save sse control word */

  xor     %rax, %rax         /* return 0 */
  ret

/* void _lh_longjmp(jmpbuf,int arg)
   edi: jmp_buf, 
   esi: arg 
*/
_lh_longjmp:                  
  movq  %rsi, %rax            /* return arg to rax */
  
  movq   8 (%edi), %rbx       /* restore registers */
  movq  24 (%edi), %rbp
  movq  32 (%edi), %r12
  movq  40 (%edi), %r13
  movq  48 (%edi), %r14
  movq  56 (%edi), %r15

  fnclex                      /* clear fpu exception flags */
  fldcw   64 (%edi)           /* restore fpu control word */
  ldmxcsr 68 (%edi)           /* restore sse control word */

  testl %eax, %eax            /* longjmp should never return 0 */ 
  jnz   ok
  incl  %eax
ok:
  movq  16 (%edi), %rsp       /* restore the stack pointer */     
  jmpl *(%edi)                /* and jump to eip  */
