; ----------------------------------------------------------------------------
; Copyright (c) 2016, Microsoft Research, Daan Leijen
; This is free software; you can redistribute it and/or modify it under the
; terms of the Apache License, Version 2.0. A copy of the License can be
; found in the file "license.txt" at the root of this distribution.
; -----------------------------------------------------------------------------

; -------------------------------------------------------
; Code for x86 (ia32) cdecl calling convention: Win32 (MSVC)
; Used on win32; also restores the exception handler chain at FS:[0]
; see: https://en.wikipedia.org/wiki/X86_calling_conventions
;
; jump_buf layout, somewhat compatible with msvc
;  0: ebp
;  4: ebx
;  8: edi
; 12: esi
; 16: esp
; 20: eip
; 24: sse control word
; 28: fpu control word
; 30: unused
; 32: registration node
; 36: sizeof jmp_buf
; -------------------------------------------------------

.386
.XMM            
.MODEL FLAT, C 
.CODE 
ASSUME FS:NOTHING

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
  
  stmxcsr [ecx+24]         ; save sse control word
  fnstcw  [ecx+28]         ; save fpu control word

  mov     eax, fs:[0]      ; save registration node (exception handlers top)
  mov     [ecx+32], eax
    
  xor     eax, eax         ; return zero
  ret

_lh_setjmp ENDP


; called with jmp_buf at esp+4, and arg at sp+8
_lh_longjmp PROC
  mov     eax, [esp+8]        ; set eax to the return value (arg)
  mov     ecx, [esp+4]        ; ecx to jmp_buf
  
  mov     ebx, [ecx+32]       ; restore registration node (exception handlers top)
  mov     fs:[0], ebx 

  mov     ebp, [ecx+ 0]       ; restore registers
  mov     ebx, [ecx+ 4]
  mov     edi, [ecx+ 8]
  mov     esi, [ecx+12]
  
  ldmxcsr [ecx+24]            ; load sse control word
  fnclex                      ; clear fpu exception flags
  fldcw   [ecx+28]            ; restore fpu control word
  
  test    eax, eax            ; longjmp should never return 0
  jnz     ok
  inc     eax
ok:
  mov     esp, [ecx+16]       ; restore esp
  jmp     dword ptr [ecx+20]  ; and jump to the eip

_lh_longjmp ENDP


; Get the top exception frame below or at `base`
; esp+4: base
; returns: eax points to the top exeption handler frame below or at base
;          ebx points to the exception handler before that (greater than base)
; clobbers: edx
_lh_get_exn_link PROC
  mov     edx, [esp+4]        ; edx: the base
  mov     eax, fs:[0]         ; eax: set to current exception frame top

; internal: 
; edx: base
; eax: current exception frame
_lh_get_exn_link_ PROC
  xor     ebx, ebx            ; ebx: the previous frame
  
find:  
  cmp     eax, edx            ; we found it if we are greater or equal to the base
  jae     found               
  test    eax, eax            ; if the current is at 0 or -1 we reached the end of the frames
  jz      found
  mov     ebx, eax            ; go to the next frame
  mov     eax, [ebx]
  cmp     eax, ebx            ; break if the new frame is up on the stack (should never happen, except for NULL)
  ja      find    
  test    eax, eax
  jz      found

notfound:
  xor     ebx, ebx

found:
  ret
_lh_get_exn_link_ ENDP
_lh_get_exn_link ENDP


; called with:
; esp+12: link to top exception frame beyond our restored stack
; esp+8 : bottom of our restored stack
; esp+4 : jmp_buf
; esp   : return address
_lh_longjmp_ex PROC
  mov     ecx, [esp+4]        ; `ecx` to `jmp_buf`
  mov     esi, [esp+12]       ; `esi` to the link
  test    esi, esi            ; don't do anything if it is NULL
  jz      notfound
  
  mov     edx, [esp+8]        ; set `edx` to the bottom of our restored stack
  mov     eax, [ecx+32]       ; set `eax` to the top exception handler frame we are going to restore
  call    _lh_get_exn_link_   ; find bottom exception handler frame in our restored stack (in `ebx`)

  test    ebx, ebx            ; check if we found it
  jz      notfound

  cmp     esi, ebx            ; sanity check: ensure link is below on the stack
  jbe     notfound

  mov     [ebx], esi          ; restore the exception handler links

notfound:
  mov     eax, 1
  mov     [esp+8], eax        ; modify argument in place
  jmp     _lh_longjmp         ; and jump to `_lh_longjmp(jmp_buf,1)`
_lh_longjmp_ex ENDP

END 