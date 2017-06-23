/* ----------------------------------------------------------------------------
  Copyright (c) 2016, 2017, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

/*
Code for ARM 64-bit as specified by ARM (used on Linux etc.)
See:
- <https://en.wikipedia.org/wiki/Calling_convention#ARM_.28A64.29>
- <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0055c/IHI0055C_beta_aapcs64.pdf>

note: according to the ARM ABI specification, only the bottom 64 bits of the floating 
      point registers need to be preserved (sec. 5.1.2 of aapcs64)

jump_buf layout:
   0: x19  
   8: x20
  16: x21
  24: x22
  32: x23
  40: x24
  48: x25
  56: x26
  64: x27
  72: x28
  80: fp   = x29
  88: lr   = x30
  96: sp   = x31
 104: fpcr
 112: fpsr
 120: d8  (64 bits)
 128: d9
 ...
 176: d15
 184: sizeof jmp_buf
*/

.global _lh_setjmp
.global _lh_longjmp
.type _lh_setjmp,%function
.type _lh_longjmp,%function

/* called with x0: &jmp_buf */
_lh_setjmp:                 
    stp   x19, x20, [x0], #16
    stp   x21, x22, [x0], #16
    stp   x23, x24, [x0], #16
    stp   x25, x26, [x0], #16
    stp   x27, x28, [x0], #16
    stp   x29, x30, [x0], #16   /* fp and lr */
    mov   x10, sp               /* sp */
    str   x10, [x0], #8
    /* store fp control and status */
    mrs   x10, fpcr
    mrs   x11, fpsr
    stp   x10, x11, [x0], #16    
    /* store float registers */
    stp   d8,  d9,  [x0], #16
    stp   d10, d11, [x0], #16
    stp   d12, d13, [x0], #16
    stp   d14, d15, [x0], #16
    /* always return zero */
    mov   x0, #0
    ret                         /* jump to lr */

    
/* called with x0: &jmp_buf, x1: return code */
_lh_longjmp:                  
    ldp   x19, x20, [x0], #16
    ldp   x21, x22, [x0], #16
    ldp   x23, x24, [x0], #16
    ldp   x25, x26, [x0], #16
    ldp   x27, x28, [x0], #16
    ldp   x29, x30, [x0], #16   /* fp and lr */
    ldr   x10, [x0], #8         /* sp */
    mov   sp,  x10
    /* load fp control and status */
    ldp   x10, x11, [x0], #16
    msr   fpcr, x10
    msr   fpsr, x11
    /* load float registers */
    ldp   d8,  d9,  [x0], #16
    ldp   d10, d11, [x0], #16
    ldp   d12, d13, [x0], #16
    ldp   d14, d15, [x0], #16
    /* never return zero */
    mov   x0, x1
    cmp   w1, #0
    cinc  w0, w1, eq
    ret                         /* jump to lr */
