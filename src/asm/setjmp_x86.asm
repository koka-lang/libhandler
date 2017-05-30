; ----------------------------------------------------------------------------
; Copyright (c) 2016, Microsoft Research, Daan Leijen
; This is free software; you can redistribute it and/or modify it under the
; terms of the Apache License, Version 2.0. A copy of the License can be
; found in the file "license.txt" at the root of this distribution.
; -----------------------------------------------------------------------------

; -------------------------------------------------------
; Code for x86 (ia32) cdecl calling convention: Win32 (MSVC)
; see: https://en.wikipedia.org/wiki/X86_calling_conventions
;
; jump_buf layout, somewhat compatible with msvc
;  0: ebp
;  4: ebx
;  8: edi
; 12: esi
; 16: esp
; 20: eip
; 24: fpu control word
; 26: unused
; 28: (sizeof jump_buf)
; -------------------------------------------------------

.386
.MODEL FLAT, C 
.CODE 

; called with jmp_buf at sp+4
_lh_setjmp PROC
  mov     ecx, [esp+4]     ; jmp_buf to ecx
  mov     eax, [esp]       ; eip: save the return address
  mov     [ecx+20], eax      

  lea     eax, [esp+4]     ; save esp (minus return address)
  mov     [ecx+16], eax

  mov     [ecx+ 0], ebp    ; save registers
  mov     [ecx+ 4], ebx    
  mov     [ecx+ 8], edi
  mov     [ecx+12], esi
  
  fnstcw  [ecx+24]         ; save fpu control word
    
  xor     eax, eax         ; return zero
  ret

_lh_setjmp ENDP


; called with jmp_buf at esp+4, and arg at sp+8
_lh_longjmp PROC
  mov     eax, [esp+8]        ; set eax to the return value (arg)
  mov     ecx, [esp+4]        ; ecx to jmp_buf
    
  mov     ebp, [ecx+ 0]       ; restore registers
  mov     ebx, [ecx+ 4]
  mov     edi, [ecx+ 8]
  mov     esi, [ecx+12]
  
  fnclex                      ; clear fpu exception flags
  fldcw   [ecx+24]            ; restore fpu control word
   
  test    eax, eax            ; longjmp should never return 0
  jnz     ok
  inc     eax
ok:
  mov     esp, [ecx+16]       ; restore esp
  jmp     dword ptr [ecx+20]  ; and jump to the eip

_lh_longjmp ENDP

END 