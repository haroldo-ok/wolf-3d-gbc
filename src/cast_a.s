; cast_a.s - the common path of a ray (bank 5, with the caster's tables): the ray's set up, the DDA (dd_run, bank 0),
; and for a plain wall the texture, the texture column, the height and the palette, stored in the ray tables.  A door
; or the moving push wall is left to the C code (render.c).
;
; This module must be the first of bank 5 (it is linked before the C code): the quarter square table must be page
; aligned (tools/checkmap.py checks it).  Also here: the wall loop (draw_walls_a) and the sprites' projection
; (project_a).
	.module	cast_a
	.globl	_cr_ray, _cr_sq, _dd_run
	.globl	_dd_var, _dd_sx, _dd_sy, _dd_tx, _dd_ty, _dd_ptr, _dd_t, _dd_side, _dd_val
	.globl	_recip_q8, _wall_x_tex, _wall_y_tex, _hgt_a, _hgt_b, _hgt_c
	.globl	_ray_t, _ray_tex, _ray_u, _ray_h, _ray_pal, _draw_walls_a
	.globl	_scaler_data, _scaler_small, _scaler_tall, _tex_bank, _draw_left, _draw_right
	.globl	_dc_fb, _dc_sc, _dc_hi, _dc_bank, _dc_n

; HRAM (render.h): set by the C code for the frame
CR_I   = 0xFFC1				; the ray
CR_FX  = 0xFFC2				; the fraction of the view position
CR_FY  = 0xFFC3
CR_MAP = 0xFFC4				; the tile map pointer of the view position
CR_FAM = 0xFFC6				; the level's texture -> palette table
CR_AX  = 0xFFC8				; |rx|, |ry| of the ray
CR_AY  = 0xFFCA
CR_TMP = 0xFFCC
CR_RX  = 0xFFCE				; the ray direction (Q8), stepped by cr_ray from the second ray on
CR_RY  = 0xFFD0
CR_FXL = 0xFFD2				; ... the fractions below Q8
CR_FYL = 0xFFD3
CR_SXL = 0xFFD4				; the step between rays: fraction, Q8 (x), fraction, Q8 (y)
CR_SX  = 0xFFD5
CR_SYL = 0xFFD7
CR_SY  = 0xFFD8
CW_I   = 0xFFDA				; draw_walls_a: the ray, the column, the first row and the rows of the left ray
CW_COL = 0xFFDB
CW_TOP = 0xFFDD
CW_N   = 0xFFDE

DOORW_FRAME = 58

	.area	_BANK5
; n * n / 4 for n = 0..511: the low bytes (two pages), then the high bytes (two pages)
SQ = 0x40				; (the page of _cr_sq: tools/checkmap.py checks it)
_cr_sq::
	.db	0x00,0x00,0x01,0x02,0x04,0x06,0x09,0x0C,0x10,0x14,0x19,0x1E,0x24,0x2A,0x31,0x38
	.db	0x40,0x48,0x51,0x5A,0x64,0x6E,0x79,0x84,0x90,0x9C,0xA9,0xB6,0xC4,0xD2,0xE1,0xF0
	.db	0x00,0x10,0x21,0x32,0x44,0x56,0x69,0x7C,0x90,0xA4,0xB9,0xCE,0xE4,0xFA,0x11,0x28
	.db	0x40,0x58,0x71,0x8A,0xA4,0xBE,0xD9,0xF4,0x10,0x2C,0x49,0x66,0x84,0xA2,0xC1,0xE0
	.db	0x00,0x20,0x41,0x62,0x84,0xA6,0xC9,0xEC,0x10,0x34,0x59,0x7E,0xA4,0xCA,0xF1,0x18
	.db	0x40,0x68,0x91,0xBA,0xE4,0x0E,0x39,0x64,0x90,0xBC,0xE9,0x16,0x44,0x72,0xA1,0xD0
	.db	0x00,0x30,0x61,0x92,0xC4,0xF6,0x29,0x5C,0x90,0xC4,0xF9,0x2E,0x64,0x9A,0xD1,0x08
	.db	0x40,0x78,0xB1,0xEA,0x24,0x5E,0x99,0xD4,0x10,0x4C,0x89,0xC6,0x04,0x42,0x81,0xC0
	.db	0x00,0x40,0x81,0xC2,0x04,0x46,0x89,0xCC,0x10,0x54,0x99,0xDE,0x24,0x6A,0xB1,0xF8
	.db	0x40,0x88,0xD1,0x1A,0x64,0xAE,0xF9,0x44,0x90,0xDC,0x29,0x76,0xC4,0x12,0x61,0xB0
	.db	0x00,0x50,0xA1,0xF2,0x44,0x96,0xE9,0x3C,0x90,0xE4,0x39,0x8E,0xE4,0x3A,0x91,0xE8
	.db	0x40,0x98,0xF1,0x4A,0xA4,0xFE,0x59,0xB4,0x10,0x6C,0xC9,0x26,0x84,0xE2,0x41,0xA0
	.db	0x00,0x60,0xC1,0x22,0x84,0xE6,0x49,0xAC,0x10,0x74,0xD9,0x3E,0xA4,0x0A,0x71,0xD8
	.db	0x40,0xA8,0x11,0x7A,0xE4,0x4E,0xB9,0x24,0x90,0xFC,0x69,0xD6,0x44,0xB2,0x21,0x90
	.db	0x00,0x70,0xE1,0x52,0xC4,0x36,0xA9,0x1C,0x90,0x04,0x79,0xEE,0x64,0xDA,0x51,0xC8
	.db	0x40,0xB8,0x31,0xAA,0x24,0x9E,0x19,0x94,0x10,0x8C,0x09,0x86,0x04,0x82,0x01,0x80
	.db	0x00,0x80,0x01,0x82,0x04,0x86,0x09,0x8C,0x10,0x94,0x19,0x9E,0x24,0xAA,0x31,0xB8
	.db	0x40,0xC8,0x51,0xDA,0x64,0xEE,0x79,0x04,0x90,0x1C,0xA9,0x36,0xC4,0x52,0xE1,0x70
	.db	0x00,0x90,0x21,0xB2,0x44,0xD6,0x69,0xFC,0x90,0x24,0xB9,0x4E,0xE4,0x7A,0x11,0xA8
	.db	0x40,0xD8,0x71,0x0A,0xA4,0x3E,0xD9,0x74,0x10,0xAC,0x49,0xE6,0x84,0x22,0xC1,0x60
	.db	0x00,0xA0,0x41,0xE2,0x84,0x26,0xC9,0x6C,0x10,0xB4,0x59,0xFE,0xA4,0x4A,0xF1,0x98
	.db	0x40,0xE8,0x91,0x3A,0xE4,0x8E,0x39,0xE4,0x90,0x3C,0xE9,0x96,0x44,0xF2,0xA1,0x50
	.db	0x00,0xB0,0x61,0x12,0xC4,0x76,0x29,0xDC,0x90,0x44,0xF9,0xAE,0x64,0x1A,0xD1,0x88
	.db	0x40,0xF8,0xB1,0x6A,0x24,0xDE,0x99,0x54,0x10,0xCC,0x89,0x46,0x04,0xC2,0x81,0x40
	.db	0x00,0xC0,0x81,0x42,0x04,0xC6,0x89,0x4C,0x10,0xD4,0x99,0x5E,0x24,0xEA,0xB1,0x78
	.db	0x40,0x08,0xD1,0x9A,0x64,0x2E,0xF9,0xC4,0x90,0x5C,0x29,0xF6,0xC4,0x92,0x61,0x30
	.db	0x00,0xD0,0xA1,0x72,0x44,0x16,0xE9,0xBC,0x90,0x64,0x39,0x0E,0xE4,0xBA,0x91,0x68
	.db	0x40,0x18,0xF1,0xCA,0xA4,0x7E,0x59,0x34,0x10,0xEC,0xC9,0xA6,0x84,0x62,0x41,0x20
	.db	0x00,0xE0,0xC1,0xA2,0x84,0x66,0x49,0x2C,0x10,0xF4,0xD9,0xBE,0xA4,0x8A,0x71,0x58
	.db	0x40,0x28,0x11,0xFA,0xE4,0xCE,0xB9,0xA4,0x90,0x7C,0x69,0x56,0x44,0x32,0x21,0x10
	.db	0x00,0xF0,0xE1,0xD2,0xC4,0xB6,0xA9,0x9C,0x90,0x84,0x79,0x6E,0x64,0x5A,0x51,0x48
	.db	0x40,0x38,0x31,0x2A,0x24,0x1E,0x19,0x14,0x10,0x0C,0x09,0x06,0x04,0x02,0x01,0x00
	.db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	.db	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	.db	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,0x02
	.db	0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03
	.db	0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x06
	.db	0x06,0x06,0x06,0x06,0x06,0x07,0x07,0x07,0x07,0x07,0x07,0x08,0x08,0x08,0x08,0x08
	.db	0x09,0x09,0x09,0x09,0x09,0x09,0x0A,0x0A,0x0A,0x0A,0x0A,0x0B,0x0B,0x0B,0x0B,0x0C
	.db	0x0C,0x0C,0x0C,0x0C,0x0D,0x0D,0x0D,0x0D,0x0E,0x0E,0x0E,0x0E,0x0F,0x0F,0x0F,0x0F
	.db	0x10,0x10,0x10,0x10,0x11,0x11,0x11,0x11,0x12,0x12,0x12,0x12,0x13,0x13,0x13,0x13
	.db	0x14,0x14,0x14,0x15,0x15,0x15,0x15,0x16,0x16,0x16,0x17,0x17,0x17,0x18,0x18,0x18
	.db	0x19,0x19,0x19,0x19,0x1A,0x1A,0x1A,0x1B,0x1B,0x1B,0x1C,0x1C,0x1C,0x1D,0x1D,0x1D
	.db	0x1E,0x1E,0x1E,0x1F,0x1F,0x1F,0x20,0x20,0x21,0x21,0x21,0x22,0x22,0x22,0x23,0x23
	.db	0x24,0x24,0x24,0x25,0x25,0x25,0x26,0x26,0x27,0x27,0x27,0x28,0x28,0x29,0x29,0x29
	.db	0x2A,0x2A,0x2B,0x2B,0x2B,0x2C,0x2C,0x2D,0x2D,0x2D,0x2E,0x2E,0x2F,0x2F,0x30,0x30
	.db	0x31,0x31,0x31,0x32,0x32,0x33,0x33,0x34,0x34,0x35,0x35,0x35,0x36,0x36,0x37,0x37
	.db	0x38,0x38,0x39,0x39,0x3A,0x3A,0x3B,0x3B,0x3C,0x3C,0x3D,0x3D,0x3E,0x3E,0x3F,0x3F
	.db	0x40,0x40,0x41,0x41,0x42,0x42,0x43,0x43,0x44,0x44,0x45,0x45,0x46,0x46,0x47,0x47
	.db	0x48,0x48,0x49,0x49,0x4A,0x4A,0x4B,0x4C,0x4C,0x4D,0x4D,0x4E,0x4E,0x4F,0x4F,0x50
	.db	0x51,0x51,0x52,0x52,0x53,0x53,0x54,0x54,0x55,0x56,0x56,0x57,0x57,0x58,0x59,0x59
	.db	0x5A,0x5A,0x5B,0x5C,0x5C,0x5D,0x5D,0x5E,0x5F,0x5F,0x60,0x60,0x61,0x62,0x62,0x63
	.db	0x64,0x64,0x65,0x65,0x66,0x67,0x67,0x68,0x69,0x69,0x6A,0x6A,0x6B,0x6C,0x6C,0x6D
	.db	0x6E,0x6E,0x6F,0x70,0x70,0x71,0x72,0x72,0x73,0x74,0x74,0x75,0x76,0x76,0x77,0x78
	.db	0x79,0x79,0x7A,0x7B,0x7B,0x7C,0x7D,0x7D,0x7E,0x7F,0x7F,0x80,0x81,0x82,0x82,0x83
	.db	0x84,0x84,0x85,0x86,0x87,0x87,0x88,0x89,0x8A,0x8A,0x8B,0x8C,0x8D,0x8D,0x8E,0x8F
	.db	0x90,0x90,0x91,0x92,0x93,0x93,0x94,0x95,0x96,0x96,0x97,0x98,0x99,0x99,0x9A,0x9B
	.db	0x9C,0x9D,0x9D,0x9E,0x9F,0xA0,0xA0,0xA1,0xA2,0xA3,0xA4,0xA4,0xA5,0xA6,0xA7,0xA8
	.db	0xA9,0xA9,0xAA,0xAB,0xAC,0xAD,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB2,0xB3,0xB4,0xB5
	.db	0xB6,0xB7,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBD,0xBE,0xBF,0xC0,0xC1,0xC2,0xC3
	.db	0xC4,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1
	.db	0xD2,0xD3,0xD4,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,0xE0
	.db	0xE1,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF
	.db	0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF

; DE = B * C (A, HL destroyed): x * y = f(x + y) - f(|x - y|); the macro inline, mul8 to call
.macro MUL8 lab
	ld	a,b
	add	a,c
	ld	l,a
	ld	a,#SQ
	adc	a,#0
	ld	h,a
	ld	e,(hl)
	inc	h
	inc	h
	ld	d,(hl)
	ld	a,b
	sub	a,c
	jr	nc,lab
	cpl
	inc	a
lab:	ld	l,a
	ld	h,#SQ
	ld	a,e
	sub	a,(hl)
	ld	e,a
	inc	h
	inc	h
	ld	a,d
	sbc	a,(hl)
	ld	d,a
.endm

mul8:
	ld	a,b
	add	a,c
	ld	l,a
	ld	a,#SQ
	adc	a,#0
	ld	h,a
	ld	e,(hl)
	inc	h
	inc	h
	ld	d,(hl)
	ld	a,b
	sub	a,c
	jr	nc,1$
	cpl
	inc	a
1$:	ld	l,a
	ld	h,#SQ
	ld	a,e
	sub	a,(hl)
	ld	e,a
	inc	h
	inc	h
	ld	a,d
	sbc	a,(hl)
	ld	d,a
	ret

; A = the low byte of B * C (E, HL destroyed)
mul8lo:
	ld	a,b
	sub	a,c
	jr	nc,1$
	cpl
	inc	a
1$:	ld	l,a
	ld	h,#SQ
	ld	e,(hl)
	ld	a,b
	add	a,c
	ld	l,a
	ld	a,#SQ
	adc	a,#0
	ld	h,a
	ld	a,(hl)
	sub	a,e
	ret

; HL = (B * DE) >> 8, B < 256 (the distance to the first boundary in the ray's parameter)
scale:
	push	de
	ld	c,e
	MUL8	sc1			; B * lo: its high byte
	ld	a,d
	pop	de
	push	af
	ld	c,d
	MUL8	sc2			; B * hi
	pop	af
	add	a,e
	ld	l,a
	ld	a,d
	adc	a,#0
	ld	h,a
	ret

; one axis of the ray: DE = the direction component (Q8), A = the fraction of the view position along it
;   -> HL = |component| at absadr, the boundary distance at sadr, the step at tadr, var |= bit when negative
.macro AXIS absadr, sadr, tadr, vb, lab
	ld	c,a			; the fraction
	bit	7,d
	jr	z,lab'p
	xor	a,a			; negative: |d|, distance = the fraction
	sub	a,e
	ld	e,a
	ld	a,#0
	sbc	a,d
	ld	d,a
	ld	a,(_dd_var)
	or	a,#vb
	ld	(_dd_var),a
	ld	b,c
	jr	lab'r
lab'p:	xor	a,a			; positive: distance = 256 - the fraction (256 when it is 0)
	sub	a,c
	ld	b,a
lab'r:	ld	a,e
	ldh	(absadr),a
	ld	a,d
	ldh	(absadr + 1),a
	ld	h,d
	ld	l,e
	add	hl,hl
	ld	de,#_recip_q8
	add	hl,de
	ld	a,(hl+)
	ld	e,a
	ld	d,(hl)			; the step between boundaries: recip_q8[|component|]
	ld	a,e
	ld	(tadr),a
	ld	a,d
	ld	(tadr + 1),a
	ld	a,b
	or	a,a
	jr	nz,lab's
	ld	a,(_dd_var)		; the fraction is 0: distance 256 (positive: the step) or 0 (negative)
	and	a,#vb
	jr	nz,lab'z
	ld	h,d
	ld	l,e
	jr	lab'w
lab'z:	ld	hl,#0
	jr	lab'w
lab's:	call	scale
lab'w:	ld	a,l
	ld	(sadr),a
	ld	a,h
	ld	(sadr + 1),a
.endm

; u8 cr_ray(void): ray CR_I (from the second ray on, CR_RX / CR_RY are stepped first); 0 when the ray's results are
; stored, 1 when the DDA stopped at a door or the push wall (the C code goes on from there, with CR_RX / CR_RY)
.macro STEP fl, sl, s, r
	ldh	a,(sl)
	ld	b,a
	ldh	a,(fl)
	add	a,b
	ldh	(fl),a
	ldh	a,(s)
	ld	b,a
	ldh	a,(r)
	adc	a,b
	ldh	(r),a
	ldh	a,(s + 1)
	ld	b,a
	ldh	a,(r + 1)
	adc	a,b
	ldh	(r + 1),a
.endm

_cr_ray::
	ldh	a,(CR_I)
	or	a,a
	jr	z,0$
	STEP	CR_FXL, CR_SXL, CR_SX, CR_RX
	STEP	CR_FYL, CR_SYL, CR_SY, CR_RY
0$:	ldh	a,(CR_RY)
	ld	c,a
	ldh	a,(CR_RY + 1)
	ld	b,a
	ldh	a,(CR_RX)
	ld	e,a
	ldh	a,(CR_RX + 1)
	ld	d,a
	push	bc
	xor	a,a
	ld	(_dd_var),a
	ldh	a,(CR_FX)
	AXIS	CR_AX, _dd_sx, _dd_tx, 1, ax
	pop	de
	ldh	a,(CR_FY)
	AXIS	CR_AY, _dd_sy, _dd_ty, 2, ay
	ldh	a,(CR_MAP)
	ld	(_dd_ptr),a
	ldh	a,(CR_MAP + 1)
	ld	(_dd_ptr + 1),a
	call	_dd_run
	ld	a,(_dd_val)
	bit	7,a
	jr	z,1$
	ld	a,#1			; a door or the push wall: the C code
	ret
1$:	; ---- a wall: its texture (the door frame on a face beside a door)
	ld	c,a			; the tile value
	and	a,#0x3F
	ld	e,a
	ld	d,#0
	ld	a,(_dd_side)
	or	a,a
	ld	hl,#_wall_x_tex
	jr	z,2$
	ld	hl,#_wall_y_tex
2$:	add	hl,de
	ld	b,(hl)			; the texture
	bit	6,c
	jr	z,5$
	ld	hl,#_dd_ptr		; beside a door: the tile the ray came from
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a
	ld	a,(_dd_side)
	or	a,a
	ld	a,(_dd_var)
	jr	z,3$
	ld	de,#1			; y side: +-1
	and	a,#2
	jr	nz,4$
	ld	de,#-1
	jr	4$
3$:	ld	de,#64			; x side: +-64
	and	a,#1
	jr	nz,4$
	ld	de,#-64
4$:	add	hl,de
	ld	a,(hl)
	and	a,#0xC0
	cp	a,#0x80
	jr	nz,5$
	ld	b,#DOORW_FRAME
	ld	a,(_dd_side)
	or	a,a
	jr	nz,5$
	inc	b
5$:	ld	a,b
	inc	a
	jr	nz,6$
	ld	b,a			; 0xFF -> 0
6$:	ld	a,b
	ldh	(CR_TMP),a
	; ---- the texture column: u = (the fraction + (t * r) >> 8) >> 2 along the wall, r the other component
	ld	a,(_dd_side)
	or	a,a
	jr	nz,7$
	ldh	a,(CR_AY)		; x side: along y
	ld	c,a
	ldh	a,(CR_AY + 1)
	ld	d,a
	ld	a,(_dd_var)
	and	a,#2
	ld	e,a			; e: the component is negative
	ldh	a,(CR_FY)
	jr	8$
7$:	ldh	a,(CR_AX)		; y side: along x
	ld	c,a
	ldh	a,(CR_AX + 1)
	ld	d,a
	ld	a,(_dd_var)
	and	a,#1
	ld	e,a
	ldh	a,(CR_FX)
8$:	push	af			; the fraction
	push	de			; d = |r| high, e = sign
	; byte 1 of t * |r| = hi(tl * rl) + lo(th * rl) + tl * rh
	ld	a,(_dd_t)
	ld	b,a
	MUL8	ur1			; tl * rl (c = rl)
	ld	a,d
	ldh	(CR_TMP + 1),a
	ld	a,(_dd_t + 1)
	ld	b,a
	call	mul8lo			; th * rl
	ld	b,a
	ldh	a,(CR_TMP + 1)
	add	a,b
	ld	b,a
	pop	de
	ld	a,(_dd_t)
	ld	c,a
	ld	a,d
	or	a,a
	jr	z,10$
9$:	ld	a,b			; + tl * rh (rh = 1 or 2)
	add	a,c
	ld	b,a
	dec	d
	jr	nz,9$
10$:	pop	af			; the fraction +- that
	bit	1,e
	jr	nz,11$
	bit	0,e
	jr	nz,11$
	add	a,b
	jr	12$
11$:	sub	a,b
12$:	srl	a
	srl	a			; 0..63
	ld	c,a
	ld	a,(_dd_side)		; the side: x -> mirrored when the ray goes -x; y -> mirrored when it goes +y
	or	a,a
	ld	a,(_dd_var)
	jr	nz,13$
	and	a,#1
	jr	z,15$
	jr	14$
13$:	and	a,#2
	jr	nz,15$
14$:	ld	a,#63
	sub	a,c
	ld	c,a
15$:	; ---- store: ray_u, ray_tex, ray_pal, ray_t, ray_h
	ldh	a,(CR_I)
	ld	e,a
	ld	d,#0
	ld	hl,#_ray_u
	add	hl,de
	ld	(hl),c
	ldh	a,(CR_TMP)
	ld	b,a			; the texture
	ld	hl,#_ray_tex
	add	hl,de
	ld	(hl),b
	ldh	a,(CR_FAM)
	ld	l,a
	ldh	a,(CR_FAM + 1)
	ld	h,a
	ld	c,b
	ld	b,#0
	add	hl,bc
	ld	b,(hl)
	ld	hl,#_ray_pal
	add	hl,de
	ld	(hl),b
	ld	hl,#_ray_t
	add	hl,de
	add	hl,de
	ld	a,(_dd_t)
	ld	c,a
	ld	(hl+),a
	ld	a,(_dd_t + 1)
	ld	b,a
	ld	(hl),a
	; the height
	call	hgt_bc
	ld	hl,#_ray_h
	add	hl,de
	ld	(hl),a
	xor	a,a
	ret

; A = hgt(BC) as render.c: the height of a wall (or sprite) at distance BC (Q8); HL destroyed, DE kept
hgt_bc:
	ld	a,b
	cp	a,#2
	jr	nc,20$
	rra				; t < 512: hgt_a[t >> 1] (b <= 1)
	ld	a,c
	rra
	ld	c,a
	ld	b,#0
	ld	hl,#_hgt_a
	jr	23$
20$:	cp	a,#8
	jr	nc,21$
	sub	a,#2			; t < 2048: hgt_b[(t - 512) >> 3]
	ld	b,a
	ld	a,c
	srl	b
	rra
	srl	b
	rra
	srl	b
	rra
	ld	c,a
	ld	b,#0
	ld	hl,#_hgt_b
	jr	23$
21$:	cp	a,#0x40
	jr	nc,22$
	sub	a,#8			; t < 16384: hgt_c[(t - 2048) >> 6]
	add	a,a
	add	a,a
	ld	b,a
	ld	a,c
	rlca
	rlca
	and	a,#3
	or	a,b
	ld	c,a
	ld	b,#0
	ld	hl,#_hgt_c
	jr	23$
22$:	ld	a,#2
	ret
23$:	add	hl,bc
	ld	a,(hl)
	ret

; ------------------------------------------------------------------ the sprites: projection
PJ_ACC = 0xFFE0				; a sum of products, 24 bits signed, little endian
PJ_DX  = 0xFFE3
PJ_DY  = 0xFFE5
PJ_D   = 0xFFE7				; the depth (Q8)
PJ_L   = 0xFFE9				; the lateral position (Q8)
PJ_T   = 0xFFEB				; a product
PJ_S   = 0xFFED				; the sign of the product
PR_CX  = 0xFFEE				; results (render.c): the screen x of the centre, the height
PR_H   = 0xFFF0
FOCAL  = 110

; PJ_ACC +-= DE (s16) * the Q8 sine or cosine at (HL) (|value| <= 256)
.macro MULSC ng, lab
	ld	a,(hl+)
	ld	c,a
	ld	a,(hl)
	ld	b,a
	xor	a,d
.if ng
	xor	a,#0x80
.endif
	ldh	(PJ_S),a		; bit 7: subtract
	bit	7,d
	jr	z,lab'1
	xor	a,a			; |DE|
	sub	a,e
	ld	e,a
	ld	a,#0
	sbc	a,d
	ld	d,a
lab'1:	bit	7,b
	jr	z,lab'2
	xor	a,a			; |BC|
	sub	a,c
	ld	c,a
	ld	a,#0
	sbc	a,b
	ld	b,a
lab'2:	ld	a,b
	or	a,a
	jr	z,lab'3
	ld	c,d			; |factor| = 256: |DE| << 8
	ld	h,e
	ld	l,#0
	jr	lab'4
lab'3:	ld	b,e			; lo * c
	push	de
	MUL8	lab'7
	ld	a,e
	ldh	(PJ_T),a
	ld	a,d
	ldh	(PJ_T + 1),a
	pop	de
	ld	b,d			; hi * c
	MUL8	lab'8
	ldh	a,(PJ_T)
	ld	l,a
	ldh	a,(PJ_T + 1)
	add	a,e
	ld	h,a
	ld	a,d
	adc	a,#0
	ld	c,a			; C:H:L = |product|
lab'4:	ldh	a,(PJ_S)
	rla
	jr	c,lab'5
	ldh	a,(PJ_ACC)
	add	a,l
	ldh	(PJ_ACC),a
	ldh	a,(PJ_ACC + 1)
	adc	a,h
	ldh	(PJ_ACC + 1),a
	ldh	a,(PJ_ACC + 2)
	adc	a,c
	ldh	(PJ_ACC + 2),a
	jr	lab'6
lab'5:	ldh	a,(PJ_ACC)
	sub	a,l
	ldh	(PJ_ACC),a
	ldh	a,(PJ_ACC + 1)
	sbc	a,h
	ldh	(PJ_ACC + 1),a
	ldh	a,(PJ_ACC + 2)
	sbc	a,c
	ldh	(PJ_ACC + 2),a
lab'6:
.endm

.macro LOADX adr
	ldh	a,(adr)
	ld	e,a
	ldh	a,(adr + 1)
	ld	d,a
.endm

.macro ACC0
	xor	a,a
	ldh	(PJ_ACC),a
	ldh	(PJ_ACC + 1),a
	ldh	(PJ_ACC + 2),a
.endm

; u8 project_a(s16 dx (DE), s16 dy (BC)): the sprite at dx, dy (Q8 tiles from the camera) -> PR_CX, PR_H, PJ_D as
; render.c's project() did with smul16 and udiv32_16; 0 when it cannot be seen
	.globl	_project_a, _sinv, _cosv
_project_a::
	ld	a,e
	ldh	(PJ_DX),a
	ld	a,d
	ldh	(PJ_DX + 1),a
	ld	a,c
	ldh	(PJ_DY),a
	ld	a,b
	ldh	(PJ_DY + 1),a
	ACC0				; depth = (dx * cos - dy * sin) >> 8
	ld	hl,#_cosv
	MULSC	0, mja
	LOADX	PJ_DY
	ld	hl,#_sinv
	MULSC	1, mjb
	ldh	a,(PJ_ACC + 2)
	bit	7,a
	jp	nz,pjno			; < 0
	ld	b,a
	ldh	a,(PJ_ACC + 1)
	ld	c,a
	ldh	(PJ_D),a
	ld	a,b
	ldh	(PJ_D + 1),a
	or	a,a
	jr	nz,1$
	ld	a,c
	cp	a,#64
	jp	c,pjno			; depth < 64
1$:	ACC0				; lat = (dx * sin + dy * cos) >> 8
	LOADX	PJ_DX
	ld	hl,#_sinv
	MULSC	0, mjc
	LOADX	PJ_DY
	ld	hl,#_cosv
	MULSC	0, mjd
	ldh	a,(PJ_ACC + 1)
	ld	l,a
	ldh	(PJ_L),a
	ldh	a,(PJ_ACC + 2)
	ld	h,a
	ldh	(PJ_L + 1),a
	bit	7,h			; |lat|
	jr	z,2$
	xor	a,a
	sub	a,l
	ld	l,a
	ld	a,#0
	sbc	a,h
	ld	h,a
2$:	ldh	a,(PJ_D)		; |lat| <= depth + 160
	add	a,#160
	ld	e,a
	ldh	a,(PJ_D + 1)
	adc	a,#0
	ld	d,a
	ld	a,e
	sub	a,l
	ld	a,d
	sbc	a,h
	jp	c,pjno
	; n = |lat| * FOCAL, 24 bits: (hi * F) << 8 + lo * F
	ld	c,#FOCAL
	ld	b,l
	push	hl
	call	mul8
	ld	a,e
	ldh	(PJ_T),a		; n, low byte
	ld	a,d
	ldh	(PJ_T + 1),a
	pop	hl
	ld	b,h
	call	mul8
	ldh	a,(PJ_T + 1)
	add	a,e
	ld	l,a
	ld	a,d
	adc	a,#0
	ld	h,a			; HL = n >> 8
	ldh	a,(PJ_D)
	ld	e,a
	ldh	a,(PJ_D + 1)
	ld	d,a			; DE = depth
	ld	a,l			; n >> 8 >= depth: the quotient is 256 or more, off the view
	sub	a,e
	ld	a,h
	sbc	a,d
	jp	nc,pjno
	ldh	a,(PJ_T)
	ld	c,a			; q = n / depth: the low byte of n shifted in, the quotient shifted into C
	ld	b,#8
3$:	sla	c
	rl	l
	rl	h
	ld	a,l
	sub	a,e
	ld	a,h
	sbc	a,d
	jr	c,4$
	ld	a,l
	sub	a,e
	ld	l,a
	ld	a,h
	sbc	a,d
	ld	h,a
	inc	c
4$:	dec	b
	jr	nz,3$
	; cx = 80 +- q
	ldh	a,(PJ_L + 1)
	bit	7,a
	ld	a,#80
	jr	nz,5$
	add	a,c
	ld	l,a
	ld	a,#0
	adc	a,#0
	ld	h,a
	jr	6$
5$:	sub	a,c
	ld	l,a
	ld	a,#0
	sbc	a,#0
	ld	h,a
6$:	ld	a,l
	ldh	(PR_CX),a
	ld	a,h
	ldh	(PR_CX + 1),a
	ldh	a,(PJ_D)		; h = hgt(depth)
	ld	c,a
	ldh	a,(PJ_D + 1)
	ld	b,a
	push	hl
	call	hgt_bc
	pop	hl
	ldh	(PR_H),a
	srl	a
	ld	e,a
	ld	d,#0			; DE = h / 2
	push	hl
	add	hl,de			; cx + h / 2 < 0: off the view
	bit	7,h
	pop	hl
	jr	nz,pjno
	ld	a,l			; cx - h / 2 >= 160: off the view
	sub	a,e
	ld	l,a
	ld	a,h
	sbc	a,d
	ld	h,a
	bit	7,h
	jr	nz,7$
	or	a,a
	jr	nz,pjno
	ld	a,l
	cp	a,#160
	jr	nc,pjno
7$:	ld	a,#1
	ret
pjno:	xor	a,a
	ret

; ------------------------------------------------------------------ the walls
; clear B rows (2 bytes each) at HL, B >= 1: unrolled by 8 (WRAM bank 3 mapped)
clrrows:
	ld	a,b
	and	a,#7
	ld	c,a
	ld	a,b
	add	a,#7
	rrca
	rrca
	rrca
	and	a,#0x1F
	ld	b,a			; groups of 8, the first one partial
	ld	a,c
	or	a,a
	jr	z,2$
	cpl
	add	a,#9			; rows skipped in the first group: 8 - n % 8, 2 bytes of code each
	add	a,a
2$:	ld	e,a
	ld	d,#0
	push	hl
	ld	hl,#3$
	add	hl,de
	ld	d,h
	ld	e,l
	pop	hl
	push	de
	xor	a,a
	ret
3$:	.rept	8
	ld	(hl+),a
	ld	(hl+),a
	.endm
	dec	b
	jr	nz,3$
	ret

; void draw_walls_a(void): the 40 rays into the frame buffer (its WRAM bank mapped).  The buffer keeps, for each
; tile column, the rows drawn the last time it was used (SP_TOP / SP_BOT at 0xDF00 / 0xDF20 of its bank: the walls
; and render.c's sprites): the left ray of a column clears those that it does not cover now (the rest are 0), the
; right one is ORed in.
SP_TOP = 0xDF00
SP_BOT = 0xDF20

_draw_walls_a::
	xor	a,a
	ldh	(CW_I),a
	ldh	(CW_COL),a
	ld	a,#0xD0
	ldh	(CW_COL + 1),a
1$:	ldh	a,(CW_I)
	ld	e,a
	ld	d,#0
	ld	hl,#_ray_h
	add	hl,de
	ld	a,(hl)
	or	a,a
	jp	z,20$			; nothing
	cp	a,#97
	jr	nc,2$
	ld	c,a			; h <= 96: scaler_small[h], top = 48 - h / 2, h rows
	ldh	(CW_N),a
	srl	a
	cpl
	add	a,#49
	ldh	(CW_TOP),a
	ld	l,c
	ld	h,#0
	add	hl,hl
	ld	de,#_scaler_small
	jr	3$
2$:	sub	a,#92			; taller: scaler_tall[k - 1], k = (h - 96 + 4) / 8 in 1..20, from the top, 96 rows
	rrca
	rrca
	rrca
	and	a,#0x1F
	jr	nz,4$
	inc	a
4$:	dec	a
	add	a,a
	ld	l,a
	ld	h,#0
	xor	a,a
	ldh	(CW_TOP),a
	ld	a,#96
	ldh	(CW_N),a
	ld	de,#_scaler_tall
3$:	add	hl,de
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a
	ld	de,#_scaler_data
	add	hl,de
	ld	a,l
	ld	(_dc_sc),a
	ld	a,h
	ld	(_dc_sc + 1),a
	ldh	a,(CW_N)
	ld	(_dc_n),a
	ldh	a,(CW_I)		; the texture column and the texture's bank
	ld	e,a
	ld	d,#0
	ld	hl,#_ray_u
	add	hl,de
	ld	a,(hl)
	add	a,#0x40
	ld	(_dc_hi),a
	ld	hl,#_ray_tex
	add	hl,de
	ld	e,(hl)
	ld	hl,#_tex_bank
	add	hl,de
	ld	a,(hl)
	ld	(_dc_bank),a
	ldh	a,(CW_TOP)		; the first row: the column + 2 * top
	add	a,a
	ld	e,a
	ldh	a,(CW_COL)
	ld	l,a
	ldh	a,(CW_COL + 1)
	ld	h,a
	add	hl,de
	ld	a,l
	ld	(_dc_fb),a
	ld	a,h
	ld	(_dc_fb + 1),a
	ldh	a,(CW_TOP)
	ld	c,a			; C = top, B = bottom (exclusive)
	ld	b,a
	ldh	a,(CW_N)
	add	a,b
	ld	b,a
	ldh	a,(CW_I)
	rra
	jr	c,10$
	call	spanclr			; left: clear what was drawn before outside the new rows, then store
	call	_draw_left
	jr	30$
10$:	call	spanadd			; right: the column's rows grow, then OR
	call	_draw_right
	jr	30$
20$:	ldh	a,(CW_I)		; no wall: the left ray clears what was drawn
	rra
	jr	c,30$
	ld	bc,#0x3030		; (an empty span)
	call	spanclr
30$:	ldh	a,(CW_I)
	inc	a
	ldh	(CW_I),a
	cp	a,#40
	ret	z
	rra
	jp	c,1$			; the right ray of the same column
	ldh	a,(CW_COL)		; the next column: + 192
	add	a,#192
	ldh	(CW_COL),a
	ldh	a,(CW_COL + 1)
	adc	a,#0
	ldh	(CW_COL + 1),a
	jp	1$

; HL = SP_TOP + the column (CW_I / 2)
spanptr:
	ldh	a,(CW_I)
	srl	a
	add	a,#<SP_TOP
	ld	l,a
	ld	h,#>SP_TOP
	ret

; the left ray's rows C..B-1: clear the rows of the old span [ot, ob) outside them, the span becomes [C, B)
spanclr:
	call	spanptr
	ld	a,(hl)			; ot
	ld	(hl),c
	ld	e,a
	set	5,l
	ld	a,(hl)			; ob
	ld	(hl),b
	ld	d,a			; old span [E, D), new [C, B)
	push	bc
	push	de
	ld	a,d			; the rows above: [ot, min(ob, nt))
	cp	a,c
	jr	c,1$
	ld	a,c
1$:	sub	a,e
	jr	c,2$
	jr	z,2$
	ld	b,a
	ld	a,e
	call	clrfrom
2$:	pop	de
	pop	bc
	ld	a,e			; the rows below: [max(ot, nb), ob)
	cp	a,b
	jr	nc,3$
	ld	a,b
3$:	ld	e,a
	ld	a,d
	sub	a,e
	ret	c
	ret	z
	ld	b,a
	ld	a,e
	jp	clrfrom

; the right ray's rows C..B-1 join the column's span
spanadd:
	call	spanptr
	ld	a,c
	cp	a,(hl)
	jr	nc,1$
	ld	(hl),a
1$:	set	5,l
	ld	a,(hl)
	cp	a,b
	ret	nc
	ld	(hl),b
	ret

; clear B rows from row A of the current column
clrfrom:
	add	a,a
	ld	e,a
	ld	d,#0
	ldh	a,(CW_COL)
	ld	l,a
	ldh	a,(CW_COL + 1)
	ld	h,a
	add	hl,de
	jp	clrrows
