; mathz_a.s - 16 x 16 multiply and 32 / 16 divide (sm83, bank 0).  SDCC convention: first 16 bit argument in DE,
; second in BC, 32 bit result in DE (high) : BC (low), 16 bit result in BC.
	.module	mathz_a
	.area	_CODE
	.globl	_umul16, _umulq8, _smul16, _udiv_nd, _dv_n, _dv_d

; u32 umul16(u16 a (DE), u16 b (BC)) -> DEBC.  Four 8 x 8 products by quarter squares, x * y = f(x + y) - f(|x - y|)
; with f(n) = n * n / 4 from the tables in WRAM bank 5 (mul_init): low bytes at 0xD000, high bytes at 0xD200, 512
; entries each.  The products with a zero high byte are skipped (most calls have one 8 bit operand).
SVBK  = 0x70
SQLO  = 0xD0
MA_L  = 0xFFB0				; HRAM temporaries
MA_H  = 0xFFB1
MB_L  = 0xFFB2
MB_H  = 0xFFB3
MR0   = 0xFFB4
MR1   = 0xFFB5
MR2   = 0xFFB6
MR3   = 0xFFB7

; mul8: DE = B * C (A, H, L destroyed; B, C kept).  WRAM bank 5 must be mapped.
mul8:
	ld	a,b
	add	a,c
	ld	l,a
	ld	a,#SQLO
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
	ld	h,#SQLO
	ld	a,e
	sub	a,(hl)
	ld	e,a
	inc	h
	inc	h
	ld	a,d
	sbc	a,(hl)
	ld	d,a
	ret

_umul16::
	ldh	a,(SVBK)
	push	af
	ld	a,#5
	ldh	(SVBK),a
	ld	a,e
	ldh	(MA_L),a
	ld	a,d
	ldh	(MA_H),a
	ld	a,c
	ldh	(MB_L),a
	ld	a,b
	ldh	(MB_H),a
	ld	b,e			; al * bl
	call	mul8
	ld	a,e
	ldh	(MR0),a
	ld	a,d
	ldh	(MR1),a
	xor	a,a
	ldh	(MR2),a
	ldh	(MR3),a
	ldh	a,(MA_H)		; ah * bl (<< 8)
	or	a,a
	jr	z,1$
	ld	b,a
	call	mul8
	ldh	a,(MR1)
	add	a,e
	ldh	(MR1),a
	ld	a,d
	adc	a,#0
	ldh	(MR2),a
	ldh	a,(MB_H)		; ah * bh (<< 16)
	or	a,a
	jr	z,2$
	ld	c,a
	call	mul8
	ldh	a,(MR2)
	add	a,e
	ldh	(MR2),a
	ld	a,d
	adc	a,#0
	ldh	(MR3),a
1$:	ldh	a,(MB_H)		; al * bh (<< 8)
	or	a,a
	jr	z,2$
	ld	c,a
	ldh	a,(MA_L)
	ld	b,a
	call	mul8
	ldh	a,(MR1)
	add	a,e
	ldh	(MR1),a
	ldh	a,(MR2)
	adc	a,d
	ldh	(MR2),a
	ldh	a,(MR3)
	adc	a,#0
	ldh	(MR3),a
2$:	ldh	a,(MR0)
	ld	c,a
	ldh	a,(MR1)
	ld	b,a
	ldh	a,(MR2)
	ld	e,a
	ldh	a,(MR3)
	ld	d,a
	pop	af
	ldh	(SVBK),a
	ret

; u16 umulq8(u16 t (DE), u16 r (BC)) -> BC = (t * r) >> 8, the low 16 bits:
;   hi(tl * rl) + th * rl + ((tl * rh + (th * rh << 8)) when rh != 0)
_umulq8::
	ldh	a,(SVBK)
	push	af
	ld	a,#5
	ldh	(SVBK),a
	ld	a,d
	ldh	(MA_H),a
	ld	a,e
	ldh	(MA_L),a
	ld	a,b
	ldh	(MB_H),a
	ld	b,e			; tl * rl: only its high byte
	call	mul8
	ld	a,d
	ldh	(MR0),a
	xor	a,a
	ldh	(MR1),a
	ldh	a,(MA_H)		; th * rl
	or	a,a
	jr	z,1$
	ld	b,a
	call	mul8
	ldh	a,(MR0)
	add	a,e
	ldh	(MR0),a
	ld	a,d
	adc	a,#0
	ldh	(MR1),a
1$:	ldh	a,(MB_H)		; rh != 0: tl * rh + (th * rh << 8)
	or	a,a
	jr	z,2$
	ld	c,a
	ldh	a,(MA_L)
	ld	b,a
	call	mul8
	ldh	a,(MR0)
	add	a,e
	ldh	(MR0),a
	ldh	a,(MR1)
	adc	a,d
	ldh	(MR1),a
	ldh	a,(MA_H)
	ld	b,a
	call	mul8
	ldh	a,(MR1)
	add	a,e
	ldh	(MR1),a
2$:	ldh	a,(MR0)
	ld	c,a
	ldh	a,(MR1)
	ld	b,a
	pop	af
	ldh	(SVBK),a
	ret


; long smul16(int a (DE), int b (BC)) -> DEBC
_smul16::
	ld	a,d
	xor	a,b
	push	af			; bit 7: the sign of the product
	bit	7,d
	jr	z,1$
	xor	a,a
	sub	a,e
	ld	e,a
	ld	a,#0
	sbc	a,d
	ld	d,a
1$:	bit	7,b
	jr	z,2$
	xor	a,a
	sub	a,c
	ld	c,a
	ld	a,#0
	sbc	a,b
	ld	b,a
2$:	call	_umul16
	pop	af
	bit	7,a
	ret	z
	xor	a,a			; negate DEBC
	sub	a,c
	ld	c,a
	ld	a,#0
	sbc	a,b
	ld	b,a
	ld	a,#0
	sbc	a,e
	ld	e,a
	ld	a,#0
	sbc	a,d
	ld	d,a
	ret

; u16 udiv_nd(void): dv_n / dv_d for dv_n < dv_d << 16
	.area	_DATA
_dv_n::	.ds	4
_dv_d::	.ds	2
_dv_cnt:	.ds	1
	.area	_CODE
_udiv_nd::
	ld	hl,#_dv_n
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	inc	hl
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a			; hl = high word, de = low word
	push	hl
	ld	hl,#_dv_d
	ld	a,(hl+)
	ld	b,(hl)
	ld	c,a			; bc = divisor
	pop	hl
	ld	a,#16
	ld	(_dv_cnt),a
1$:	sla	e
	rl	d
	rl	l
	rl	h
	jr	c,3$
	ld	a,l
	sub	a,c
	ld	l,a
	ld	a,h
	sbc	a,b
	ld	h,a
	jr	nc,2$
	ld	a,l
	add	a,c
	ld	l,a
	ld	a,h
	adc	a,b
	ld	h,a
	jr	4$
3$:	ld	a,l
	sub	a,c
	ld	l,a
	ld	a,h
	sbc	a,b
	ld	h,a
2$:	inc	e
4$:	ld	a,(_dv_cnt)
	dec	a
	ld	(_dv_cnt),a
	jr	nz,1$
	ld	b,d
	ld	c,e
	ret
