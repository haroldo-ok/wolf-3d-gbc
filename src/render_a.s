; render_a.s - the hot loops of the 3-D view (sm83, bank 0)
;
; Frame buffer: WRAM bank 3 at 0xD000, column major tiles: column tx (0..19) is 192 bytes, pixel row r of it at +2*r
; (plane 0, plane 1).  A ray is a 4 pixel strip: the left one in the high nibble, the right one in the low nibble.
	.module	render_a
	.area	_CODE

	.globl	_dd_run, _dd_var, _dd_sx, _dd_sy, _dd_tx, _dd_ty, _dd_ptr, _dd_t, _dd_side, _dd_val
	.globl	_draw_left, _draw_right, _spr_left, _spr_right, _pair_empty, _clr32
	.globl	_dc_fb, _dc_sc, _dc_hi, _dc_s, _dc_bank, _dc_old, _dc_n, _cur_rom_bank

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
_dc_n::		.ds	1		; draw: rows of a wall column (1..96)

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
; rows of one ray: _dc_n rows (1..96) of scaler bytes (2*texel row) -> texture page (high byte _dc_hi); the frame
; buffer in HL, the texture in BC (B the page), the scaler in DE.  Unrolled by 8 (entered in the middle for the rows
; that are not a multiple of 8), the groups of 8 counted in HRAM.
DC_CNT = 0xFFC0
LROW = 8				; bytes of a row of the loops below
RROW = 12

; the entry of an unrolled loop of 8 rows: A = rows (1..96), C = bytes a row -> the target address pushed
.macro DUFF lab, rowsz
	ld	c,a
	add	a,#7
	rrca
	rrca
	rrca
	and	a,#0x1F
	ldh	(DC_CNT),a		; groups of 8 (the first one partial)
	ld	a,c
	and	a,#7
	jr	z,1$
	cpl
	add	a,#9			; rows skipped in the first group: 8 - n % 8
	ld	c,a
	add	a,a
	add	a,a
	add	a,a			; * 8
.if rowsz - 8
	add	a,c
	add	a,c
	add	a,c
	add	a,c			; * 12
.endif
1$:	add	a,#<lab
	ld	l,a
	ld	a,#0
	adc	a,#>lab
	ld	h,a
	push	hl
	ld	hl,#_dc_sc
	ld	a,(hl+)
	ld	e,a
	ld	a,(hl+)
	ld	d,a
	ld	b,(hl)			; _dc_hi
	ld	hl,#_dc_fb
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a
	ret				; into the loop
.endm

_draw_left::
	BANKIN
	ld	a,(_dc_n)
	DUFF	2$, LROW
2$:	.rept	8			; the left ray comes first on a cleared column: store, no need to OR
	ld	a,(de)
	inc	de
	ld	c,a
	ld	a,(bc)
	ld	(hl+),a
	inc	c
	ld	a,(bc)
	ld	(hl+),a
	.endm
	ldh	a,(DC_CNT)
	dec	a
	ldh	(DC_CNT),a
	jr	nz,2$
	jp	drawdone

_draw_right::
	BANKIN
	ld	a,(_dc_n)
	DUFF	2$, RROW
2$:	.rept	8
	ld	a,(de)
	inc	de
	or	a,#0x80			; the right ray's copy of the texture: the high half of the page
	ld	c,a
	ld	a,(bc)
	or	a,(hl)
	ld	(hl+),a
	inc	c
	ld	a,(bc)
	or	a,(hl)
	ld	(hl+),a
	.endm
	ldh	a,(DC_CNT)
	dec	a
	ldh	(DC_CNT),a
	jr	nz,2$
	jp	drawdone

; ------------------------------------------------------------------ sprite columns
; the column (_dc_s: 128 bytes, 128-byte aligned, two bytes a texel row: plane 0 and plane 1 as 0x00 / 0xFF, or 0x55
; for a transparent texel) is drawn through the scaler into the 4 pixels of the strip (mask S), over what is there:
; byte = ((texel ^ byte) & S) ^ byte.  HL frame buffer, BC column (B its page, C the row), DE scaler.
SPR_CLEAR = 0x55

.macro SPR_LOOP odd, s, lab
lab'r:	ld	a,(de)
	inc	de
	bit	7,a
	jp	nz,drawdone
.if odd
	or	a,#0x80
.endif
	ld	c,a
	ld	a,(bc)
	cp	a,#SPR_CLEAR
	jr	z,lab't
	xor	a,(hl)
	and	a,#s
	xor	a,(hl)
	ld	(hl+),a
	inc	c
	ld	a,(bc)
	xor	a,(hl)
	and	a,#s
	xor	a,(hl)
	ld	(hl+),a
	jr	lab'r
lab't:	inc	hl
	inc	hl
	jr	lab'r
.endm

; DE = scaler, B = page of the column, HL = frame buffer; carry = the column is the odd one of its page.  The scaler
; rows above the column's first drawn row (kept in a transparent row 0) are skipped.
sprsetup:
	ld	hl,#_dc_sc
	ld	a,(hl+)
	ld	e,a
	ld	d,(hl)
	ld	hl,#(_dc_s + 1)
	ld	a,(hl-)
	ld	b,a
	ld	c,(hl)
	ld	hl,#_dc_fb
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a
	ld	a,(bc)			; row 0
	cp	a,#SPR_CLEAR
	jr	nz,2$
	inc	c
	ld	a,(bc)			; 2 * the first drawn row
	dec	c
	push	bc
	ld	c,a
1$:	ld	a,(de)			; (the end of the scaler, 0xFF, stops this too)
	cp	a,c
	jr	nc,3$
	inc	de
	inc	hl
	inc	hl
	jr	1$
3$:	pop	bc
2$:	ld	a,c
	rla
	ret

_spr_left::
	BANKIN
	call	sprsetup
	jr	c,sl1r
	SPR_LOOP 0, 0xF0, sl0
	SPR_LOOP 1, 0xF0, sl1

_spr_right::
	BANKIN
	call	sprsetup
	jr	c,sr1r
	SPR_LOOP 0, 0x0F, sr0
	SPR_LOOP 1, 0x0F, sr1

; ------------------------------------------------------------------ the enemies' tile buffers (rspr.c)
; u8 pair_empty(u16 p (DE)): 1 when the 32 bytes at p are all 0
_pair_empty::
	ld	h,d
	ld	l,e
	xor	a,a
	ld	b,#8
1$:	or	a,(hl)
	inc	hl
	or	a,(hl)
	inc	hl
	or	a,(hl)
	inc	hl
	or	a,(hl)
	inc	hl
	dec	b
	jr	nz,1$
	or	a,a
	ld	a,#0
	ret	nz
	inc	a
	ret

; void clr32(u16 p (DE), u16 n (BC)): clear n blocks of 32 bytes at p (n >= 1)
_clr32::
	ld	h,d
	ld	l,e
	xor	a,a
1$:	.rept	32
	ld	(hl+),a
	.endm
	dec	c
	jr	nz,1$
	ret
