; ----------------------------------------------------------------------------
; Copyright (c) 2016, Microsoft Research, Daan Leijen
; This is free software; you can redistribute it and/or modify it under the
; terms of the Apache License, Version 2.0. A copy of the License can be
; found in the file "license.txt" at the root of this distribution.
; -----------------------------------------------------------------------------

; -------------------------------------------------------
; Code for x64 (x86_64) calling convention as used on Windows 
; see: 
; - https://en.wikipedia.org/wiki/X86_calling_conventions
; - https://msdn.microsoft.com/en-us/library/ms235286.aspx
; - http://www.agner.org/optimize/calling_conventions.pdf
;
; note: we use 'movdqu' instead of 'movdqa' since we cannot
;       guarantee proper alignment.
;
; jump_buf layout, compatible with msvc
;   0: rdx (frame pointer on msvc)
;   8: rbx
;  16: rsp
;  24: rbp
;  32: rsi
;  40: rdi
;  48: r12
;  56: r13
;  64: r14
;  72: r15
;  80: rip
;  88: mxcrs, sse control word (32 bit)
;  92: fpcr, fpu control word (16 bit)
;  94: unused (16 bit)
;  96: xmm6
;  ... (128-bit sse registers)
; 240: xmm15
; 256: sizeof jmp_buf
; -------------------------------------------------------

.CODE 

; called with jmp_buf in ecx, msvc puts the frame pointer in edx
_lh_setjmp PROC
  mov     rax, [rsp]       ; rip: save the return address
  mov     [rcx+80], rax      

  lea     rax, [rsp+8]     ; save rsp (minus return address)
  mov     [rcx+16], rax

  mov     [rcx+ 0], edx    ; save registers
  mov     [rcx+ 8], rbx    
  mov     [rcx+24], rbp
  mov     [rcx+32], rsi
  mov     [rcx+40], rdi
  mov     [rcx+48], r12
  mov     [rcx+56], r13
  mov     [rcx+64], r14
  mov     [rcx+72], r15
  
  stmxcsr [rcx+88]         ; save sse control word
  fnstcw  [rcx+92]         ; save fpu control word
  
  movdqu  [rcx+96],  xmm6  ; save sse registers
  movdqu  [rcx+112], xmm7
  movdqu  [rcx+128], xmm8
  movdqu  [rcx+144], xmm9 
  movdqu  [rcx+160], xmm10
  movdqu  [rcx+176], xmm11
  movdqu  [rcx+192], xmm12
  movdqu  [rcx+208], xmm13
  movdqu  [rcx+224], xmm14
  movdqu  [rcx+240], xmm15
  
  xor     eax, eax
  ret

_lh_setjmp ENDP


; called with jmp_buf in ecx, and arg in edx (= always 1)
_lh_longjmp PROC
  mov     eax, edx              ; set rax to the return value (arg)
    
  mov     rdx,   [rcx+ 0]       ; restore registers
  mov     rbx,   [rcx+ 8]
  mov     rbp,   [rcx+24]
  mov     rsi,   [rcx+32]
  mov     rdi,   [rcx+40]
  mov     r12,   [rcx+48]
  mov     r13,   [rcx+56]
  mov     r14,   [rcx+64]
  mov     r15,   [rcx+72]
  
  ldmxcsr [rcx+88]              ; restore sse control word
  fnclex                        ; clear fpu exception flags
  fldcw   [rcx+92]              ; restore fpu control word
  
  movdqu  xmm6,  [rcx+96]       ; restore sse registers
  movdqu  xmm7,  [rcx+112]
  movdqu  xmm8,  [rcx+128]
  movdqu  xmm9,  [rcx+144]
  movdqu  xmm10, [rcx+160]
  movdqu  xmm11, [rcx+176]
  movdqu  xmm12, [rcx+192]
  movdqu  xmm13, [rcx+208]
  movdqu  xmm14, [rcx+224]
  movdqu  xmm15, [rcx+240]
   
  test    eax, eax              ; longjmp should never return 0
  jnz     ok
  inc     eax
ok:
  mov     rsp, [rcx+16]         ; restore rsp
  jmp     qword ptr [rcx+80]    ; and jump to the rip

_lh_longjmp ENDP

END 