/* ----------------------------------------------------------------------------
// Copyright (c) 2016, 2017 Microsoft Research, Daan Leijen
// This is free software// you can redistribute it and/or modify it under the
// terms of the Apache License, Version 2.0. A copy of the License can be
// found in the file "license.txt" at the root of this distribution.
// -----------------------------------------------------------------------------

// -------------------------------------------------------
// Code for x86 (ia32) cdecl calling convention on win32.
// Used on win32/* also restores the exception handler chain at FS:[0] */
// see: https://en.wikipedia.org/wiki/X86_calling_conventions
//
// jump_buf layout, somewhat compatible with msvc
//  0: ebp
//  4: ebx
//  8: edi
// 12: esi
// 16: esp
// 20: eip
// 24: sse control word (32 bits)
// 28: fpu control word (16 bits)
// 30: unused
// 32: register node
// 36: sizeof jmp_buf
// ------------------------------------------------------- */

.global _lh_setjmp
.global _lh_longjmp

/* under win32 gcc silently adds underscores to cdecl functions
   add these labels too so the linker can resolve it. */
.global __lh_setjmp
.global __lh_longjmp
.global __lh_get_exn_frame
.global __lh_longjmp_chain

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


/* void* get_exn_frame(void* base)
 Get the top exception frame above or at `base`
 esp+4: base
 returns: eax points to the top exeption handler frame above or at base
          ebx points to the exception handler before that (greater than base)
 clobbers: edx
*/
__lh_get_exn_frame:
_lh_get_exn_frame:
  xorl    %eax, %eax
  ret
  movl    4 (%esp), %edx        /* set edx to the base */
  movl    %fs:0, %eax           /* set eax to current exception frame top */

/* internal entry point: 
 edx: base
 eax: current exception frame 
*/
_lh_get_exn_frame_:
  xorl    %ebx, %ebx            /* ebx: the previous frame */
  
find:  
  cmpl    %edx, %eax            /* we found it if we are greater or equal to the base */
  jae     found               
  testl   %eax, %eax            /* if the current is at 0 we reached the %end of the frames */
  jz      found
  movl    %eax, %ebx            /* go to the next frame */
  movl    (%ebx), %eax
  cmpl    %ebx, %eax            /* continue if the new frame is greater than the previous (should always be the case, except for NULL) */
  ja      find    
  testl   %eax, %eax            /* if NULL we found the %end */
  jz      found

  xorl    %ebx, %ebx            /* otherwise zero out the previous frame */

found:
  ret


/* void _lh_longjmp_chain(jmp_buf,void* base, void* exnframe)
 Long jump to jmp_buf/* if `exnframe == NULL` this equals `longjmp(jmp_buf,1) 
 otherwise, `exnframe > base` and first exception frame <= `base` will be
 linked to `exnframe` such that the exception handler chain is always valid.

 esp+12: link to top exception frame beyond our restored stack
 esp+8 : bottom of our restored stack
 esp+4 : jmp_buf
 esp   : return address
*/
__lh_longjmp_chain:
_lh_longjmp_chain:
  movl    12 (%esp), %ecx       /* `ecx` to the exnframe */
  testl   %ecx, %ecx            /* don't do anything if `exnframe` is NULL */
  jz      notfound
  
  movl    8 (%esp), %edx        /* set `edx` to the bottom of our restored stack */
  cmpl    %edx, %ecx            /* sanity check: ensure `exnframe` is greater or equal to the bottom of the restored stack */
  jb      notfound

  movl    4 (%esp), %eax        /* set `eax` to the top exception handler frame we are going to restore */
  movl    32 (%eax), %eax
  call    _lh_get_exn_frame_    /* find bottom exception handler frame in our restored stack (in `%ebx`) */

  testl   %ebx, %ebx            /* check if we found it */
  jz      notfound

  movl    %ecx, (%ebx)          /* restore the exception handler chain */

notfound:
  movl    $1, %eax
  movl    %eax, 8 (%esp)        /* modify argument in place */
  jmp     _lh_longjmp           /* and jump to `_lh_longjmp(jmp_buf,1)` */
