/* ----------------------------------------------------------------------------
// Copyright (c) 2016, Microsoft Research, Daan Leijen
// This is free software// you can redistribute it and/or modify it under the
// terms of the Apache License, Version 2.0. A copy of the License can be
// found in the file "license.txt" at the root of this distribution.
// -----------------------------------------------------------------------------

// -------------------------------------------------------
// Code for x86 (ia32) cdecl calling convention: Win32, Linux, etc.
// see: https://en.wikipedia.org/wiki/X86_calling_conventions
//
// jump_buf layout, somewhat compatible with msvc
//  0: ebp
//  4: ebx
//  8: edi
// 12: esi
// 16: esp
// 20: eip
// 24: fpu control word
// 26: unused
// 28: (sizeof jump_buf)
// ------------------------------------------------------- */

.global _lh_setjmp
.global _lh_longjmp

/* under win32 gcc silently adds underscores to cdecl functions; 
   add these labels too so the linker can resolve it. */
.global __lh_setjmp
.global __lh_longjmp

/* called with jmp_buf at sp+4 */
__lh_setjmp:
_lh_setjmp:
  movl    4 (%esp), %ecx   /* jmp_buf to ecx  */
  movl    (%esp), %eax     /* eip: save the return address */
  movl    %eax, 20 (%ecx)  

  leal    4 (%esp), %eax   /* save esp (minus return address) */
  movl    %eax, 16 (%ecx)  

  movl    %ebp,  0 (%ecx)  /* save registers */
  movl    %ebx,  4 (%ecx)
  movl    %edi,  8 (%ecx)
  movl    %esi, 12 (%ecx)

  fnstcw  24 (%ecx)        /* save fpu control word */
    
  xorl    %eax, %eax       /* return zero */
  ret


/* called with jmp_buf at esp+4, and arg at sp+8 */
__lh_longjmp:
_lh_longjmp:
  movl    8 (%esp), %eax      /* set eax to the return value (arg) */
  movl    4 (%esp), %ecx      /* set ecx to jmp_buf */
  
  movl    0 (%ecx), %ebp      /* restore registers */
  movl    4 (%ecx), %ebx
  movl    8 (%ecx), %edi
  movl    12 (%ecx), %esi

  fnclex                      /* clear fpu exception flags */
  fldcw   24 (%ecx)           /* restore fpu control word */
   
  testl   %eax, %eax          /* longjmp should never return 0 */
  jnz     ok
  incl    %eax
ok:
  movl    16 (%ecx), %esp     /* restore esp */
  jmpl    *20 (%ecx)          /* and jump to the eip */
