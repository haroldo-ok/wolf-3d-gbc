; render_a.s - the hot loops of the 3-D view (sm83, bank 0)
;
; Frame buffer: WRAM bank 3 at 0xD000, column major tiles: column tx (0..19) is 192 bytes, pixel row r of it at +2*r
; (plane 0, plane 1).  A ray is a 4 pixel strip: the left one in the high nibble, the right one in the low nibble.
	.module	render_a
	.area	_CODE

	.globl	_dd_run, _dd_var, _dd_sx, _dd_sy, _dd_tx, _dd_ty, _dd_ptr, _dd_t, _dd_side, _dd_val
	.globl	_fb_clear, _draw_left, _draw_right, _spr_left, _spr_right
	.globl	_dc_fb, _dc_sc, _dc_hi, _dc_s, _dc_bank, _dc_old, _cur_rom_bank

	.area	_DATA
_dd_var::	.ds	1		; bit 0: x step is -1 ; bit 1: y step is -1
_dd_sx::	.ds	2		; distance to the next x boundary (Q8 ray parameter)
_dd_sy::	.ds	2
_dd_tx::	.ds	2		; distance between x boundaries
_dd_ty::	.ds	2
_dd_ptr::	.ds	2		; tile map pointer (WRAM bank 2)
_dd_t::		.ds	2		; result: parameter at the crossing
_dd_side::	.ds	1		; result: 0 x step, 1 y step
_dd_val::	.ds	1		; result: the tile value
_dc_fb::	.ds	2		; draw: frame buffer pointer of the first row
_dc_sc::	.ds	2		; draw: scaler pointer
_dc_hi::	.ds	1		; draw: texture page (high byte of the address)
_dc_s::		.ds	2		; sprite: pointer to the column (ROM)
_dc_bank::	.ds	1		; ROM bank of the texture / sprite data
_dc_old::	.ds	1

	.area	_CODE

; ------------------------------------------------------------------ DDA
; step the ray until it enters a tile whose value is not 0.  State in _dd_*, the result in _dd_t, _dd_side, _dd_val;
; the state is stored again at the exit (the caller adds the step of the side that was hit to continue)
.macro XSTEP neg
.if neg
	ld	a,c
	sub	a,#64
	ld	c,a
	jr	nc,.+3
	dec	b
.else
	ld	a,c
	add	a,#64
	ld	c,a
	jr	nc,.+3
	inc	b
.endif
.endm

.macro DDA_LOOP xneg, yneg, lab
lab'loop:
	ld	a,h
	cp	a,d
	jr	c,lab'xs
	jr	nz,lab'ys
	ld	a,l
	cp	a,e
	jr	c,lab'xs
lab'ys:
.if yneg
	dec	bc
.else
	inc	bc
.endif
	ld	a,(bc)
	or	a,a
	jr	nz,lab'hity
	ld	a,(_dd_ty)
	add	a,e
	ld	e,a
	ld	a,(_dd_ty + 1)
	adc	a,d
	ld	d,a
	jr	nc,lab'loop
	ld	de,#0xFFFF
	jr	lab'loop
lab'xs:
	XSTEP	xneg
	ld	a,(bc)
	or	a,a
	jr	nz,lab'hitx
	ld	a,(_dd_tx)
	add	a,l
	ld	l,a
	ld	a,(_dd_tx + 1)
	adc	a,h
	ld	h,a
	jr	nc,lab'loop
	ld	hl,#0xFFFF
	jr	lab'loop
lab'hitx:
	ld	(_dd_val),a
	ld	a,l
	ld	(_dd_t),a
	ld	a,h
	ld	(_dd_t + 1),a
	xor	a,a
	jp	hitcommony
lab'hity:
	ld	(_dd_val),a
	ld	a,e
	ld	(_dd_t),a
	ld	a,d
	ld	(_dd_t + 1),a
	ld	a,#1
	jp	hitcommony
.endm

_dd_run::
	ld	hl,#_dd_sx
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a			; hl = sx
	push	hl
	ld	hl,#_dd_sy
	ld	a,(hl+)
	ld	d,(hl)
	ld	e,a			; de = sy
	ld	hl,#_dd_ptr
	ld	a,(hl+)
	ld	b,(hl)
	ld	c,a			; bc = ptr
	pop	hl
	ld	a,(_dd_var)
	or	a,a
	jp	z,loop00
	dec	a
	jp	z,loop10
	dec	a
	jp	z,loop01
	jp	loop11

	DDA_LOOP 0, 0, l00
	.globl	loop00
loop00 = l00loop
	DDA_LOOP 1, 0, l10
loop10 = l10loop
	DDA_LOOP 0, 1, l01
loop01 = l01loop
	DDA_LOOP 1, 1, l11
loop11 = l11loop

hitcommony:
	ld	(_dd_side),a
	ld	a,c
	ld	(_dd_ptr),a
	ld	a,b
	ld	(_dd_ptr + 1),a
	ld	a,l
	ld	(_dd_sx),a
	ld	a,h
	ld	(_dd_sx + 1),a
	ld	a,e
	ld	(_dd_sy),a
	ld	a,d
	ld	(_dd_sy + 1),a
	ret

; ------------------------------------------------------------------ frame buffer
; clear the 3840 bytes at 0xD000 (WRAM bank 3 mapped)
_fb_clear::
	ld	hl,#0xD000
	xor	a,a
	ld	b,#240
1$:	.rept	16
	ld	(hl+),a
	.endm
	dec	b
	jr	nz,1$
	ret

; map the data bank (_dc_bank) into 0x4000 for the length of a draw routine; the caller's bank comes back at the end
.macro BANKIN
	ld	a,(_cur_rom_bank)
	ld	(_dc_old),a
	ld	a,(_dc_bank)
	ld	(_cur_rom_bank),a
	ld	(0x2000),a
.endm
drawdone:
	ld	a,(_dc_old)
	ld	(_cur_rom_bank),a
	ld	(0x2000),a
	ret

; ------------------------------------------------------------------ wall columns
; rows of one ray: scaler bytes (2*texel row, terminated by a byte with bit 7 set) -> texture page (high byte _dc_hi)
_draw_left::
	BANKIN
	ld	hl,#_dc_fb
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	inc	hl
	ld	c,(hl)
	inc	hl
	ld	b,(hl)
	ld	a,(_dc_hi)
	ld	h,a
1$:	ld	a,(bc)			; the left ray comes first on a cleared column: store, no need to OR
	inc	bc
	bit	7,a
	jp	nz,drawdone
	ld	l,a
	ld	a,(hl+)
	ld	(de),a
	inc	de
	ld	a,(hl)
	ld	(de),a
	inc	de
	jr	1$

_draw_right::
	BANKIN
	ld	hl,#_dc_fb
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	inc	hl
	ld	c,(hl)
	inc	hl
	ld	b,(hl)
	ld	a,(_dc_hi)
	ld	h,a
1$:	ld	a,(bc)
	inc	bc
	bit	7,a
	jp	nz,drawdone
	or	a,#0x80
	ld	l,a
	ld	a,(de)
	or	a,(hl)
	ld	(de),a
	inc	l
	inc	de
	ld	a,(de)
	or	a,(hl)
	ld	(de),a
	inc	de
	jr	1$

; ------------------------------------------------------------------ sprite columns
; the column (64 texel values 0..3, _dc_s) is drawn through the scaler into the 4 pixels of the strip, over the walls
.macro SPR_ROW keep, set
1$:	ld	a,(bc)
	inc	bc
	bit	7,a
	jp	nz,drawdone
	srl	a			; texel row
	ld	hl,#_dc_s
	push	de
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	ld	l,a
	ld	h,#0
	add	hl,de
	pop	de
	ld	a,(hl)			; texel value
	or	a,a
	jr	nz,2$
	inc	de
	inc	de
	jr	1$
2$:	dec	a
	jr	z,3$			; value 1: plane 0
	dec	a
	jr	z,4$			; value 2: plane 1
	ld	a,(de)			; value 3: both
	and	a,#keep
	or	a,#set
	ld	(de),a
	inc	de
	ld	a,(de)
	and	a,#keep
	or	a,#set
	ld	(de),a
	inc	de
	jr	1$
3$:	ld	a,(de)
	and	a,#keep
	or	a,#set
	ld	(de),a
	inc	de
	ld	a,(de)
	and	a,#keep
	ld	(de),a
	inc	de
	jr	1$
4$:	ld	a,(de)
	and	a,#keep
	ld	(de),a
	inc	de
	ld	a,(de)
	and	a,#keep
	or	a,#set
	ld	(de),a
	inc	de
	jr	1$
.endm

_spr_left::
	BANKIN
	ld	hl,#_dc_fb
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	inc	hl
	ld	c,(hl)
	inc	hl
	ld	b,(hl)
	SPR_ROW 0x0F, 0xF0

_spr_right::
	BANKIN
	ld	hl,#_dc_fb
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	inc	hl
	ld	c,(hl)
	inc	hl
	ld	b,(hl)
	SPR_ROW 0xF0, 0x0F
