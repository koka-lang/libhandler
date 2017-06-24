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
; Used on win32; also restores the exception handler chain at FS:[0]
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

; int setjmp(jmp_buf)
; esp+4: jmp_buf
; esp+0: return address
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

  mov     eax, fs:[0]      ; save exception top frame
  mov     [ecx+32], eax
    
  xor     eax, eax         ; return zero
  ret

_lh_setjmp ENDP


; void longjmp(jmp_buf,int)
; esp+8: argument
; esp+4: jump buffer
_lh_longjmp PROC
  mov     eax, [esp+8]        ; set eax to the return value (arg)
  mov     ecx, [esp+4]        ; ecx to jmp_buf
  
  mov     ebx, [ecx+32]       ; restore the exception handler top frame
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


; void* get_exn_frame(void* base)
; Get the top exception frame above or at `base`
; esp+4: base
; returns: eax points to the top exeption handler frame above or at base
;          ebx points to the exception handler before that (greater than base)
_lh_get_exn_frame PROC
  mov     edx, [esp+4]        ; set edx to the base
  mov     eax, fs:[0]         ; set eax to current exception frame top

; internal entry point: 
; edx: base
; eax: current exception frame
_lh_get_exn_frame_ PROC
  xor     ebx, ebx            ; ebx: the previous frame
  
find:  
  cmp     eax, edx            ; we found it if we are greater or equal to the base
  jae     found               
  test    eax, eax            ; if the current is at 0 we reached the end of the frames
  jz      found
  mov     ebx, eax            ; go to the next frame
  mov     eax, [ebx]
  cmp     eax, ebx            ; continue if the new frame is greater than the previous (should always be the case, except for NULL)
  ja      find    
  test    eax, eax            ; if NULL we found the end
  jz      found

notfound:
  xor     ebx, ebx            ; otherwise zero out the previous frame

found:
  ret
_lh_get_exn_frame_ ENDP
_lh_get_exn_frame ENDP


; void _lh_longjmp_chain(jmp_buf,void* base, void* exnframe)
; Long jump to jmp_buf; if `exnframe == NULL` this equals `longjmp(jmp_buf,1)
; otherwise, `exnframe > base` and first exception frame <= `base` will be
; linked to `exnframe` such that the exception handler chain is always valid.
;
; esp+12: link to top exception frame beyond our restored stack
; esp+8 : bottom of our restored stack
; esp+4 : jmp_buf
; esp   : return address
_lh_longjmp_chain PROC
  mov     ecx, [esp+12]       ; `ecx` to the exnframe
  test    ecx, ecx            ; don't do anything if `exnframe` is NULL
  jz      notfound
  
  mov     edx, [esp+8]        ; set `edx` to the bottom of our restored stack
  cmp     ecx, edx            ; sanity check: ensure `exnframe` is greater than the bottom of the restored stack
  jb      notfound

  mov     eax, [esp+4]        ; set `eax` to the top exception handler frame we are going to restore
  mov     eax, [eax+32]       
  call    _lh_get_exn_frame_  ; find bottom exception handler frame in our restored stack (in `ebx`)

  test    ebx, ebx            ; check if we found it
  jz      notfound

  mov     [ebx], ecx          ; restore the exception handler chain

notfound:
  mov     eax, 1
  mov     [esp+8], eax        ; modify argument in place
  jmp     _lh_longjmp         ; and jump to `_lh_longjmp(jmp_buf,1)`
_lh_longjmp_chain ENDP

END 