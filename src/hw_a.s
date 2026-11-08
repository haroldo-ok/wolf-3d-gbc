; hw_a.s - interrupt helpers that need exact timing (bank 0)
	.module	hw_a
	.area	_CODE

	.globl	_stat_phase, _live_floor, _live_hud, _live_bg, _live_obj
	.globl	_cur_lcdc, _stat_isr, _pal_up_bg, _pal_up_obj, _oam_dma_install, _oam_dma, _gdma_run, _gd_src, _gd_dst, _gd_left, _hc_frame, _hc_bank, _hc_stream

BCPS = 0x68
BCPD = 0x69
OCPS = 0x6A
OCPD = 0x6B
STAT = 0x41
LYC  = 0x45

; wait for the end of mode 3 of the current line (waits for mode 3 first)
.macro WAIT_HBLANK
1$:	ldh	a,(STAT)
	and	a,#3
	cp	a,#3
	jr	nz,1$
2$:	ldh	a,(STAT)
	and	a,#3
	cp	a,#3
	jr	z,2$
.endm

; floor colour into colour 0 of palette p (b = low, d = high byte)
.macro SETP0 p
	ld	a,#(0x80 + 8*p)
	ldh	(BCPS),a
	ld	a,b
	ldh	(c),a
	ld	a,d
	ldh	(c),a
.endm

_stat_isr::
	ld	a,(_stat_phase)
	or	a,a
	jr	nz,sp1
	ld	hl,#_live_floor
	ld	b,(hl)
	inc	hl
	ld	d,(hl)
	ld	c,#BCPD
	WAIT_HBLANK
	SETP0 0
	SETP0 1
	SETP0 2
	SETP0 3
	ld	a,#47
	ldh	(LYC),a
	ld	a,#1
	ld	(_stat_phase),a
	ret
sp1:
	cp	a,#1
	jr	nz,sp2
	ld	hl,#_live_floor
	ld	b,(hl)
	inc	hl
	ld	d,(hl)
	ld	c,#BCPD
	WAIT_HBLANK
	SETP0 4
	SETP0 5
	SETP0 6
	SETP0 7
	ld	a,#95
	ldh	(LYC),a
	ld	a,#2
	ld	(_stat_phase),a
	ret
sp2:
	cp	a,#2
	jr	nz,sp3
	ld	hl,#(_live_hud + 48)
	ld	c,#BCPD
	WAIT_HBLANK
	ld	a,(_cur_lcdc)
	and	a,#0xEF
	ldh	(0x40),a
	ld	a,#(0x80 + 48)
	ldh	(BCPS),a
	.rept	16
	ld	a,(hl+)
	ldh	(c),a
	.endm
	ld	a,#96
	ldh	(LYC),a
	ld	a,#3
	ld	(_stat_phase),a
	ret
sp3:
	cp	a,#3
	jr	nz,sp4
	ld	hl,#_live_hud
	ld	b,#0x80
	ld	d,#97
	ld	e,#4
	jr	hudp
sp4:
	cp	a,#4
	jr	nz,sp5
	ld	hl,#(_live_hud + 16)
	ld	b,#0x90
	ld	d,#98
	ld	e,#5
	jr	hudp
sp5:
	cp	a,#5
	jr	nz,sp6
	ld	hl,#(_live_hud + 32)
	ld	b,#0xA0
	ld	d,#0
	ld	e,#6
hudp:
	ld	c,#BCPD
	WAIT_HBLANK
	ld	a,b
	ldh	(BCPS),a
	.rept	16
	ld	a,(hl+)
	ldh	(c),a
	.endm
	ld	a,d
	or	a,a
	jr	nz,hp1
	ldh	(STAT),a
	jr	hp2
hp1:	ldh	(LYC),a
hp2:	ld	a,e
	ld	(_stat_phase),a
sp6:
	ret

_pal_up_bg::
	ld	hl,#_live_bg
	ld	a,#0x80
	ldh	(BCPS),a
	ld	c,#BCPD
	.rept	64			; unrolled: this runs in every VBlank of the 3-D view
	ld	a,(hl+)
	ldh	(c),a
	.endm
	ret
_pal_up_obj::
	ld	hl,#_live_obj
	ld	a,#0x80
	ldh	(OCPS),a
	ld	c,#OCPD
	ld	b,#64
1$:	ld	a,(hl+)
	ldh	(c),a
	dec	b
	jr	nz,1$
	ret

; the OAM copy from C (an inline 'call 0xFF80' hid the call from the compiler, which then kept HL across it)
; gdma_run: copies the job in gd_src / gd_dst / gd_left (blocks of 16 bytes) to VRAM in chunks of 12 blocks
; while LY is 144..150 (each chunk ends inside VBlank even if the DMA is twice as slow as on hardware); the
; globals are left at what remains.
CHUNK = 12
	.area	_DATA
_gd_src::	.ds	2
_gd_dst::	.ds	2
_gd_left::	.ds	1
	.area	_CODE
_gdma_run::
1$:	ldh	a,(0x44)
	cp	a,#144
	ret	c
	cp	a,#151
	ret	nc
	ld	a,(_gd_left)
	or	a,a
	ret	z
	ld	b,a
	cp	a,#CHUNK
	jr	c,2$
	ld	b,#CHUNK
2$:	ld	hl,#_gd_src
	ld	a,(hl+)
	ld	e,a
	ld	a,(hl)
	ldh	(0x51),a
	ld	a,e
	and	a,#0xF0
	ldh	(0x52),a
	ld	hl,#_gd_dst
	ld	a,(hl+)
	ld	c,a
	ld	a,(hl)
	and	a,#0x1F
	ldh	(0x53),a
	ld	a,c
	and	a,#0xF0
	ldh	(0x54),a
	ld	a,b
	dec	a
	ldh	(0x55),a		; general purpose DMA: the CPU waits on hardware ...
3$:	ldh	a,(0x55)		; ... but not in every emulator (mGBA): wait for the end (HDMA5 reads FF), before the
	inc	a			; registers and the ROM bank change under it
	jr	nz,3$
	ld	a,b
	swap	a
	ld	c,a			; n * 16 (n <= 12)
	ld	hl,#_gd_src
	ld	a,(hl)
	add	a,c
	ld	(hl+),a
	ld	a,(hl)
	adc	a,#0
	ld	(hl),a
	ld	hl,#_gd_dst
	ld	a,(hl)
	add	a,c
	ld	(hl+),a
	ld	a,(hl)
	adc	a,#0
	ld	(hl),a
	ld	hl,#_gd_left
	ld	a,(hl)
	sub	a,b
	ld	(hl),a
	jr	1$

; hc_frame: one frame of a hi-colour picture (tools/gb_hicolor.py): the palettes for line 0 at the end of VBlank,
; then 3 palettes (24 bytes) in every HBlank of lines 0..142, from the stream at hc_stream in ROM bank hc_bank.
; Interrupts are off from line ~150 to line 142; the frame interrupt still runs at 144.
	.area	_DATA
_hc_bank::	.ds	1
_hc_stream::	.ds	2
	.area	_CODE
_hc_frame::
1$:	ldh	a,(0x44)		; the end of VBlank (after the frame interrupt)
	cp	a,#149
	jr	c,1$
	cp	a,#153
	jr	nc,1$
	di
	ld	a,(_hc_bank)
	ld	(0x2000),a
	ld	hl,#_hc_stream
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a
	ld	c,#0x69			; BCPD
	ld	a,#0x80
	ldh	(0x68),a
	ld	b,#48			; (a loop: VBlank has the time, bank 0 not the room)
5$:	ld	a,(hl+)
	ldh	(c),a
	dec	b
	jr	nz,5$
2$:	ldh	a,(0x41)		; mode 3 of line b ...
	and	a,#3
	cp	a,#3
	jr	nz,2$
3$:	ldh	a,(0x41)		; ... then its HBlank
	and	a,#3
	jr	nz,3$
	ld	a,b
	rrca
	ld	a,#0x98			; even line: palettes 3-5 (index 24)
	jr	nc,4$
	ld	a,#0x80			; odd line: palettes 0-2
4$:	ldh	(0x68),a
	.rept	24
	ld	a,(hl+)
	ldh	(c),a
	.endm
	inc	b
	ld	a,b
	cp	a,#143
	jr	nz,2$
	ld	a,(_cur_rom_bank)
	ld	(0x2000),a
	ei
	ret

_oam_dma::
	jp	0xFF80

_oam_dma_install::
	ld	hl,#oam_stub
	ld	de,#0xFF80
	ld	b,#(oam_stub_end - oam_stub)
1$:	ld	a,(hl+)
	ld	(de),a
	inc	de
	dec	b
	jr	nz,1$
	ret
oam_stub:
	ld	a,#0xC0
	ldh	(0x46),a
	ld	a,#40
2$:	dec	a
	jr	nz,2$
	ret
oam_stub_end:
	.globl	_oam_buf
_oam_buf = 0xC000
