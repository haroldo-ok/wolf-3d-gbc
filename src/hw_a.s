; hw_a.s - interrupt helpers that need exact timing (bank 0)
	.module	hw_a
	.area	_CODE

	.globl	_stat_phase, _live_floor, _live_hud, _live_bg, _live_obj
	.globl	_cur_lcdc, _stat_isr, _pal_up_bg, _pal_up_obj, _oam_dma_install, _oam_dma, _oam_dma_a, _gdma_run, _gdma_calibrate, _gd_fast, _gd_src, _gd_dst, _gd_left, _hc_frame, _hc_bank, _hc_stream

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
	ld	a,#47			; the next line first: a late write (a long mode 3) would miss its LY = LYC
	ldh	(LYC),a
	WAIT_HBLANK
	SETP0 0
	SETP0 1
	SETP0 2
	SETP0 3
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
	ld	a,#95
	ldh	(LYC),a
	WAIT_HBLANK
	SETP0 4
	SETP0 5
	SETP0 6
	SETP0 7
	ld	a,#2
	ld	(_stat_phase),a
	ret
sp2:
	cp	a,#2
	jr	nz,sp3
	ld	hl,#(_live_hud + 48)
	ld	c,#BCPD
	ld	a,#96
	ldh	(LYC),a
	WAIT_HBLANK
	ld	a,(_cur_lcdc)		; the view uses the signed tile addressing (0x8800..), the HUD the unsigned one
	or	a,#0x10
	ldh	(0x40),a
	ld	a,#(0x80 + 48)
	ldh	(BCPS),a
	.rept	16
	ld	a,(hl+)
	ldh	(c),a
	.endm
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
	ld	a,d			; the next line first (or no more line interrupts)
	or	a,a
	jr	nz,hp1
	ldh	(STAT),a
	jr	hp2
hp1:	ldh	(LYC),a
hp2:	WAIT_HBLANK
	ld	a,b
	ldh	(BCPS),a
	.rept	16
	ld	a,(hl+)
	ldh	(c),a
	.endm
	ld	a,e
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
; gdma_run: copies the job in gd_src / gd_dst / gd_left (blocks of 16 bytes) to VRAM during VBlank, in chunks sized
; by the lines left: a block takes 32 dots on hardware (gd_fast, measured by gdma_calibrate: 14 blocks a line, the
; chunk ends before the line 0 draws); where the DMA was measured slower (some emulators: twice as slow), 7 blocks a
; line until line 152 and 3 on line 152.  The globals are left at what remains.
	.area	_DATA
_gd_src::	.ds	2
_gd_dst::	.ds	2
_gd_left::	.ds	1
_gd_fast::	.ds	1
	.area	_CODE
_gdma_run::
1$:	ldh	a,(0x44)
	sub	a,#144
	ret	c			; not in VBlank
	cp	a,#9
	ret	nc			; line 153 (or LY 0, its value for most of the line)
	ld	c,a
	ld	a,(_gd_fast)
	rra				; bit 0: fast
	ld	a,c
	jr	nc,4$
	cpl
	add	a,#10			; fast: 14 blocks a line left (9 - (LY - 144)), less 4
	ld	c,a
	swap	a
	sub	a,c
	sub	a,c			; 14 * lines
	sub	a,#4			; (10 to 122 blocks: one DMA)
	ld	c,a
	jr	6$
4$:	cp	a,#8
	jr	c,5$
	ld	c,#3			; slow, line 152
	jr	6$
5$:	cpl
	add	a,#9			; slow: lines left before line 152: 8 - (LY - 144)
	ld	c,a
	add	a,a
	add	a,a
	add	a,a
	sub	a,c
	ld	c,a			; 7 blocks a line
6$:	ld	a,(_gd_left)
	or	a,a
	ret	z
	cp	a,c
	jr	c,2$
	ld	a,c
2$:	ld	b,a
	ld	hl,#_gd_src
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
	ld	d,a
	and	a,#0xF0
	ld	c,a			; n * 16: low byte in c, high byte in d
	ld	a,d
	and	a,#0x0F
	ld	d,a
	ld	hl,#_gd_src
	ld	a,(hl)
	add	a,c
	ld	(hl+),a
	ld	a,(hl)
	adc	a,d
	ld	(hl),a
	ld	hl,#_gd_dst
	ld	a,(hl)
	add	a,c
	ld	(hl+),a
	ld	a,(hl)
	adc	a,d
	ld	(hl),a
	ld	hl,#_gd_left
	ld	a,(hl)
	sub	a,b
	ld	(hl),a
	jp	1$

; void gdma_calibrate(void): times a DMA of 42 blocks in VBlank (to the enemies' tiles in VRAM bank 1, unused
; outside the 3-D view), once: gd_fast = 0x81 when it took less than 4 lines (32 dots a block, as on hardware), 0x80
; when slower.  Nothing when the display is off (measured the next time).
_gdma_calibrate::
	ld	a,(_gd_fast)
	bit	7,a
	ret	nz			; measured already
	ldh	a,(0x40)
	bit	7,a
	ret	z
	di
1$:	ldh	a,(0x44)
	cp	a,#143
	jr	nz,1$
2$:	ldh	a,(0x44)
	cp	a,#143
	jr	z,2$			; the start of line 144
	ldh	a,(0x4F)
	ld	c,a
	ld	a,#1
	ldh	(0x4F),a
	xor	a,a
	ldh	(0x51),a		; from 0x0000 (bank 0)
	ldh	(0x52),a
	ldh	(0x53),a		; to 0x8000
	ldh	(0x54),a
	ld	a,#41
	ldh	(0x55),a
3$:	ldh	a,(0x55)
	inc	a
	jr	nz,3$
	ldh	a,(0x44)
	ld	b,a
	ld	a,c
	ldh	(0x4F),a
	ld	a,b
	cp	a,#148
	ld	a,#0x80			; (bit 7: measured)
	jr	nc,4$
	inc	a
4$:	ld	(_gd_fast),a
	ei
	ret

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
	ld	a,#0xC0
_oam_dma_a::				; void oam_dma_a(u8 hi): the OAM copy from page hi (A)
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
	ldh	(0x46),a
	ld	a,#40
2$:	dec	a
	jr	nz,2$
	ret
oam_stub_end:
	.globl	_oam_buf
_oam_buf = 0xC000
