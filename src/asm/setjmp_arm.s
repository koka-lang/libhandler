/* ----------------------------------------------------------------------------
// Copyright (c) 2016, Microsoft Research, Daan Leijen
// This is free software// you can redistribute it and/or modify it under the
// terms of the Apache License, Version 2.0. A copy of the License can be
// found in the file "license.txt" at the root of this distribution.
// -----------------------------------------------------------------------------

// -------------------------------------------------------
// Code for arm 32-bit *UNTESTED*
// See: https://en.wikipedia.org/wiki/Calling_convention#ARM_.28A32.29
//
// jump_buf layout, compatible with msvc
//  0: frame (unused)
//  4: r4
//  8: r5
// 12: r6
// 16: r7
// 20: r8
// 24: sb,    = r9, stack base, v6
// 28: sl,    = r10, stack limit, v7
// 32: fp     = r11
// 36: sp     = r13
// 40: lr     = r14 
// 44: fpscr
// 48: d8  (double, VFP/NEON registers)
// 56: d9
// ...
// 104: d15
// 112: sizeof jmp_buf
//
// ip = r12, scratch register
// ------------------------------------------------------- */

.global _lh_setjmp
.global _lh_longjmp
.type _lh_setjmp,%function
.type _lh_longjmp,%function


/* setjmp: r0 points to the jmp_buf */
_lh_setjmp:
    mov     ip,   sp
    str     lr,   [r0], #4  /* just put lr in slot 0 .. */
    stmia   r0!,  {r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}
#if defined(__VFP_FP__)     /* restore fp register */
    vmrs    r1,   fpscr
    str     r1,   [r0], #4
    vstmia  r0!,  {d8,d9,d10,d11,d12,d13,d14,d15}
#endif
    mov.w   r0,   #0     /* return 0 */
    bx      lr

/* called with r0=&jmp_buf, r1=return code */
_lh_longjmp:
    add     r0,   #4
    ldmia   r0!,  {r4,r5,r6,r7,r8,r9,r10,fp,ip,lr}
#if defined(__VFP_FP__)   /* restore fp registers */
    ldr     r0,   [r0], #4
    vmsr    fpscr, r0
    vldmia  r0!,  {d8,d9,d10,d11,d12,d13,d14,d15}
#endif
    mov     sp,   ip    
    movs    r0,   r1      /* never return zero */
    it      eq
    moveq   r0,   #1
    bx      lr
