;
; Copyright (C) 1993-1996 Id Software, Inc.
; Copyright (C) 1993-2008 Raven Software
; Copyright (C) 2016-2017 Alexey Khokholov (Nuke.YKT)
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation; either version 2
; of the License, or (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; DESCRIPTION: Assembly texture mapping routines for planar VGA mode
;

BITS 32
%include "macros.inc"

%ifdef MODE_Y
%include "defs.inc"

extern _destview
extern _centery
extern _viewheight
extern _fuzzoffset
extern _fuzzpos
extern _colormaps

;============================================================================
; unwound vertical scaling code
;
; eax   light table pointer, 0 lowbyte overwritten
; ebx   all 0, low byte overwritten
; ecx   fractional step value
; edx   fractional scale value
; esi   start of source pixels
; edi   bottom pixel in screenbuffer to blit into
;
; ebx should be set to 0 0 0 dh to feed the pipeline
;
; The graphics wrap vertically at 128 pixels
;============================================================================

BEGIN_DATA_SECTION

%macro SCALEDEFINE 1
  dd vscale%1
%endmacro

%macro TESTFUZZPOSDEFINE 1
  dd testfuzzpos%1
%endmacro

align 4

scalecalls:
  %assign LINE 0
  %rep SCREENHEIGHT+1
    SCALEDEFINE LINE
  %assign LINE LINE+1
  %endrep

BEGIN_CODE_SECTION

; ==================
; R_DrawColumnPotato
; ==================
CODE_SYM_DEF R_DrawFuzzColumnPotato
	push		edi
  push		ebx
	push		ecx
	push		edx
	push		esi
	push		ebp

  mov  ebp,[_dc_yh]
  mov  eax,[_dc_yl]
  mov  edi,[_ylookup+ebp*4]
  sub  ebp,eax         ; ebp = pixel count
  js   short .done

  add  edi,[_destview]
  add  edi,[_dc_x]

  mov   ecx,[_dc_iscale]

  sub   eax,[_centery]
  imul  ecx
  mov   edx,[_dc_texturemid]
  shl   ecx,9 ; 7 significant bits, 25 frac
  add   edx,eax
  mov   esi,[_dc_source]
  shl   edx,9 ; 7 significant bits, 25 frac
  mov   eax,[_dc_colormap]

  xor   ebx,ebx
  shld  ebx,edx,7
  jmp  [scalecalls+4+ebp*4]

.done:
	pop		ebp
	pop		esi
	pop		edx
	pop		ecx
	pop		ebx
  pop		edi
  ret
; R_DrawColumnPotato ends

; ===============
; R_DrawColumnLow
; ===============
CODE_SYM_DEF R_DrawFuzzColumnLow
  push		edi
  push		ebx
	push		ecx
	push		edx
	push		esi
	push		ebp

  mov  ebp,[_dc_yh]
  mov  ebx,[_dc_yl]
  mov  edi,[_ylookup+ebp*4]
  sub  ebp,ebx         ; ebp = pixel count
  js   short .done

  ; set plane
  mov  ecx,[_dc_x]
  add  edi,[_destview]
  mov  esi, ecx
  
  and  cl,1
  mov  al,3
  add  cl, cl
  mov  dx,SC_INDEX+1
  shl  al,cl
  out  dx,al

  shr esi,1
  mov eax, ebx
  add edi,esi

  mov   ecx,[_dc_iscale]

  sub   eax,[_centery]
  imul  ecx
  mov   edx,[_dc_texturemid]
  shl   ecx,9 ; 7 significant bits, 25 frac
  add   edx,eax
  mov   esi,[_dc_source]
  shl   edx,9 ; 7 significant bits, 25 frac
  mov   eax,[_dc_colormap]

  xor   ebx,ebx
  shld  ebx,edx,7
  jmp  [scalecalls+4+ebp*4]

.done:
	pop		ebp
	pop		esi
	pop		edx
	pop		ecx
	pop		ebx
  pop		edi
  ret
; R_DrawColumnLow ends

CODE_SYM_DEF R_DrawFuzzColumn
	push		edi
	push		ebx
	push		ecx
	push		edx
	push		esi
	push		ebp

  mov  eax,[_viewheight]
  dec  eax

  mov  ebp,[_dc_yh]

  cmp  eax,ebp
  jne  dc_yhOK

  dec  eax
  mov  ebp,eax

dc_yhOK:
  mov  ebx,[_dc_yl]

  test ebx,ebx
  jne dc_ylOK

  mov  ebx,1

dc_ylOK:
  mov  edi,[_ylookup+ebp*4]
  sub  ebp,ebx         ; ebp = pixel count
  js   short done

  mov ecx,[_dc_x]
  add edi,[_destview]
  mov esi,ecx

  ; outpw(GC_INDEX, GC_READMAP + ((dc_x & 3) << 8));
  mov eax,ecx
	and	eax,3
	shl	eax,8
	mov	dx,0x3CE
	add	eax,4
	out	dx,ax

  ; outp(SC_INDEX + 1, 1 << (dc_x & 3));
  and  cl,3
  mov  dx,SC_INDEX+1
  mov  al,1
  shl  al,cl
  out  dx,al

  shr esi,2
  add edi,esi

  xor ecx,ecx
  xor edx,edx

  mov ebx,[_colormaps]
  mov	esi,[_fuzzpos]

  jmp  [scalecalls+4+ebp*4]

done:
	pop		ebp
	pop		esi
	pop		edx
	pop		ecx
	pop		ebx
  pop		edi
  ret
; R_DrawColumn ends

%macro SCALELABEL 1
  vscale%1
%endmacro

%macro TESTFUZZPOSDEFINE 1
  testfuzzpos%1
%endmacro

%macro JMPTESTFUZZPOSDEFINE 1
  jne testfuzzpos%1
%endmacro

%assign LINE SCREENHEIGHT
%rep SCREENHEIGHT-1
  SCALELABEL LINE:

  mov		ebp,[_fuzzoffset+esi*4]
  inc   esi
	mov   cl,[ebp+edi-(LINE-1)*80]
	mov		dl,[ecx+ebx+0x600]
	mov		[edi-(LINE-1)*80],dl

  cmp   esi,0x32
  JMPTESTFUZZPOSDEFINE LINE
  xor   esi,esi

  TESTFUZZPOSDEFINE LINE:
  %assign LINE LINE-1
%endrep

vscale1:

  mov		ebp,[_fuzzoffset+esi*4]
	inc   esi
  mov   cl,[ebp+edi-(LINE-1)*80]
	mov		dl,[ecx+ebx+0x600]
	mov		[edi-(LINE-1)*80],dl
  
  cmp esi,0x32
  jne testfuzzpos1
  xor esi,esi

testfuzzpos1:
  mov   [_fuzzpos],esi
	pop	ebp
  pop	esi
  pop	edx

vscale0:
	pop	ecx
	pop	ebx
  pop	edi
  ret

%endif