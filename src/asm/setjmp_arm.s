/* ----------------------------------------------------------------------------
// Copyright (c) 2016, 2017 Microsoft Research, Daan Leijen
// This is free software// you can redistribute it and/or modify it under the
// terms of the Apache License, Version 2.0. A copy of the License can be
// found in the file "license.txt" at the root of this distribution.
// -----------------------------------------------------------------------------

// -------------------------------------------------------
// Code for ARM 32-bit with floating point (vfp)
// See: 
// - <https://en.wikipedia.org/wiki/Calling_convention#ARM_.28A32.29>
// - <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0042f/IHI0042F_aapcs.pdf>
//
// jump_buf layout, 
//  0: r4
//  4: r5
//  8: r6
// 12: r7
// 16: r8
// 20: sb,    = r9, stack base, v6
// 24: sl,    = r10, stack limit, v7
// 28: fp     = r11
// 32: ip     = r12
// 36: sp     = r13
// 40: lr     = r14 
// 44: fpscr
// 48: d8  (double, VFP/NEON registers)
// 56: d9
// ...
// 104: d15
// 112: sizeof jmp_buf
// ------------------------------------------------------- */

.global _lh_setjmp
.global _lh_longjmp
.type _lh_setjmp,%function
.type _lh_longjmp,%function


/* setjmp: r0 points to the jmp_buf */
_lh_setjmp:
    stmia   r0!, {r4-r12}
    str     r13, [r0], #4   /* sp */
    str     r14, [r0], #4   /* lr */
    /* store fp control */
    vmrs    r1,  fpscr
    str     r1,  [r0], #4
    /* store fp registers */
    vstmia  r0!, {d8-d15}
    /* return 0 */
    mov     r0, #0
    bx      lr

/* called with r0=&jmp_buf, r1=return code */
_lh_longjmp:
    ldmia   r0!, {r4-r12}
    ldr     r13, [r0], #4
    ldr     r14, [r0], #4
    /* restore fp registers */
    ldr     r2,  [r0], #4
    vmsr    fpscr, r2
    vldmia  r0!, {d8-d15}
    /* never return zero */
    movs    r0,   r1      
    it      eq
    moveq   r0,   #1
    bx      lr
