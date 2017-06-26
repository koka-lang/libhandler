/* --------------------------------------------------------------------------
Copyright (c) 2016, 2017 Microsoft Research, Daan Leijen
This is free softwareyou can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------

Code for x86 (ia32) cdecl calling convention on win32.
Used on win32. Also restores the exception handler chain at FS:[0]
see: https://en.wikipedia.org/wiki/X86_calling_conventions

jump_buf layout, somewhat compatible with msvc
 0: ebp
 4: ebx
 8: edi
12: esi
16: esp
20: eip
24: sse control word (32 bits)
28: fpu control word (16 bits)
30: unused
32: register node
36: sizeof jmp_buf
-------------------------------------------------------------------------- */

.global _lh_setjmp
.global _lh_longjmp
.global _lh_get_exn_top

/* under win32 gcc silently adds underscores to cdecl functions
   add these labels too so the linker can resolve it. */
.global __lh_setjmp
.global __lh_longjmp
.global __lh_get_exn_top

/* called with jmp_buf at sp+4 */
__lh_setjmp:
_lh_setjmp:
  movl    4 (%esp), %ecx   /* jmp_buf to ecx  */
  movl    0 (%esp), %eax   /* eip: save the return address */
  movl    %eax, 20 (%ecx)  

  leal    4 (%esp), %eax   /* save esp (minus return address) */
  movl    %eax, 16 (%ecx)  

  movl    %ebp,  0 (%ecx)  /* save registers */
  movl    %ebx,  4 (%ecx)
  movl    %edi,  8 (%ecx)
  movl    %esi, 12 (%ecx)

  stmxcsr 24 (%ecx)        /* save sse control word */
  fnstcw  28 (%ecx)        /* save fpu control word */

  movl    %fs:0, %eax      /* save registration node (exception handling frame top) */
  movl    %eax, 32 (%ecx)
    
  xorl    %eax, %eax       /* return zero */
  ret


/* called with jmp_buf at esp+4, and arg at sp+8 */
__lh_longjmp:
_lh_longjmp:
  movl    8 (%esp), %eax      /* set eax to the return value (arg) */
  movl    4 (%esp), %ecx      /* set ecx to jmp_buf */

  movl    32 (%ecx), %ebx     /* restore registration node (exception handling frame top) */
  movl    %ebx, %fs:0
  
  movl    0 (%ecx), %ebp      /* restore registers */
  movl    4 (%ecx), %ebx
  movl    8 (%ecx), %edi
  movl    12 (%ecx), %esi

  ldmxcsr 24 (%ecx)           /* restore sse control word */
  fnclex                      /* clear fpu exception flags */
  fldcw   28 (%ecx)           /* restore fpu control word */
   
  testl   %eax, %eax          /* longjmp should never return 0 */
  jnz     ok
  incl    %eax
ok:
  movl    16 (%ecx), %esp     /* restore esp */
  jmpl    *20 (%ecx)          /* and jump to the eip */


/* exn_frame* get_exn_top()
 Get the address of the top exception frame's `previous` field.
*/
__lh_get_exn_top:
_lh_get_exn_top:
  mov     %fs:0, %eax
  ret
