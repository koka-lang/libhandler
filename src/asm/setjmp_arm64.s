/* ----------------------------------------------------------------------------
  Copyright (c) 2016, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

/*
Code for ARM 64-bit  
See: https://en.wikipedia.org/wiki/Calling_convention#ARM_.28A64.29

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
 120: unused
 128: q8  (128 bits)
 144: q9
 ...
 240: q15
 256: sizeof jmp_buf
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
    add   x0, x0, #8                /* skip unused */
    /* store float registers */
    stp   q8,  q9,  [x0], #32
    stp   q10, q11, [x0], #32
    stp   q12, q13, [x0], #32
    stp   q14, q15, [x0], #32
    /* always return zero */
    mov   x0, xzr
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
    add   x0, x0, #8                /* skip unused */
    /* load float registers */
    ldp   q8,  q9,  [x0], #32
    ldp   q10, q11, [x0], #32
    ldp   q12, q13, [x0], #32
    ldp   q14, q15, [x0], #32
    /* never return zero */
    cmp   x1, #0
    csinc x0, x1, xzr, ne
    ret                         /* jump to lr */
