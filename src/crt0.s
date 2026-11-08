; crt0.s - Game Boy Color start up for SDCC sm83
	.module	crt0
	.globl	_main
	.globl	l__INITIALIZER
	.globl	s__INITIALIZER
	.globl	s__INITIALIZED
	.globl	_vbl_isr
	.globl	_stat_isr
	.globl	_tim_isr
	.globl	_cur_rom_bank

	.area	_HOME
	.area	_CODE
	.area	_INITIALIZER
	.area	_GSINIT
	.area	_GSFINAL
	.area	_DATA
	.area	_INITIALIZED
	.area	_BSEG
	.area	_BSS
	.area	_HEAP

	.area	_HEADER (ABS)
	.org	0x00
; rst 0x00: jp (hl)
	jp	(hl)
	.org	0x08
	.ds	0x38
	.org	0x40
	jp	vbl_vec
	.org	0x48
	jp	stat_vec
	.org	0x50
	jp	tim_vec
	.org	0x58
	reti
	.org	0x60
	reti
	.org	0x100
	nop
	jp	.start
; Nintendo logo, required by the boot ROM
	.org	0x104
	.db	0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,0x00,0x0C,0x00,0x0D
	.db	0x00,0x08,0x11,0x1F,0x88,0x89,0x00,0x0E,0xDC,0xCC,0x6E,0xE6,0xDD,0xDD,0xD9,0x99
	.db	0xBB,0xBB,0x67,0x63,0x6E,0x0E,0xEC,0xCC,0xDD,0xDC,0x99,0x9F,0xBB,0xB9,0x33,0x3E
	.org	0x134
	.ascii	"WOLF3D GBC"
	.db	0x00
	.org	0x13F
	.ascii	"WL3C"			; manufacturer code
	.db	0xC0			; CGB only
	.db	0x00,0x00		; licensee
	.db	0x00			; no SGB
	.db	0x1B			; MBC5 + RAM + battery
	.db	0x08			; ROM size: patched by tools/mkrom.py
	.db	0x04			; RAM size 128 KB
	.db	0x01			; non-Japanese
	.db	0x33			; old licensee code
	.db	0x00			; version
	.db	0x00			; header checksum (patched)
	.dw	0x0000			; global checksum (patched)

	.area	_HOME
vbl_vec:
	push	af
	push	bc
	push	de
	push	hl
	call	_vbl_isr
	pop	hl
	pop	de
	pop	bc
	pop	af
	reti
stat_vec:
	push	af
	push	bc
	push	de
	push	hl
	call	_stat_isr
	pop	hl
	pop	de
	pop	bc
	pop	af
	reti
tim_vec:
	push	af
	push	bc
	push	de
	push	hl
	call	_tim_isr
	pop	hl
	pop	de
	pop	bc
	pop	af
	reti

.start:
	di
	ld	sp,#0xD000
	xor	a
	ldh	(0xFF),a		; IE = 0
	ldh	(0x0F),a
	; double speed
	ld	a,#0x30
	ldh	(0x00),a
	ldh	a,(0x4D)
	bit	7,a
	jr	nz,1$
	ld	a,#1
	ldh	(0x4D),a
	stop
	nop
1$:
	; clear WRAM bank 0 and the switchable bank 1..7
	ld	a,#1
	ldh	(0x70),a
	ld	hl,#0xC000
	ld	bc,#0x1000
	xor	a
2$:	ld	(hl+),a
	dec	bc
	ld	d,a
	ld	a,b
	or	c
	ld	a,d
	jr	nz,2$
	; initialised data: ROM -> RAM
	call	gsinit
	call	_main
3$:	halt
	nop
	jr	3$

	.area	_GSINIT
gsinit::
	ld	bc,#l__INITIALIZER
	ld	a,b
	or	c
	jr	z,4$
	ld	de,#s__INITIALIZED
	ld	hl,#s__INITIALIZER
5$:	ld	a,(hl+)
	ld	(de),a
	inc	de
	dec	bc
	ld	a,b
	or	c
	jr	nz,5$
4$:
	.area	_GSFINAL
	ret

	.area	_HOME
; --- banking
	.globl	_rom_bank
	.globl	_rom_bank_swap
	.globl	___sdcc_bcall_ehl
	.globl	___sdcc_call_hl
___sdcc_call_hl::
	jp	(hl)
; call hl in ROM bank e; the bank is restored afterwards
___sdcc_bcall_ehl::
	ld	a,(_cur_rom_bank)
	push	af
	ld	a,e
	ld	(_cur_rom_bank),a
	ld	(0x2000),a
	call	___sdcc_call_hl
	ld	(_bc_tmp),a		; the result is in A, BC (16 bit) or DEBC (32 bit): keep them all
	pop	af			; a = the old bank (this popped into BC before, which broke every banked
	ld	(_cur_rom_bank),a	; function returning 16 or 32 bits)
	ld	(0x2000),a
	ld	a,(_bc_tmp)
	ret
; void rom_bank(u8 b)  (sdcccall(1): b in a)
_rom_bank::
	ld	(_cur_rom_bank),a
	ld	(0x2000),a
	ret
; u8 rom_bank_swap(u8 b): returns the previous bank
_rom_bank_swap::
	ld	e,a
	ld	a,(_cur_rom_bank)
	ld	d,a
	ld	a,e
	ld	(_cur_rom_bank),a
	ld	(0x2000),a
	ld	a,d
	ret

	.area	_DATA
_cur_rom_bank::
	.ds	1
_bc_tmp:
	.ds	1
