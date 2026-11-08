; snd_a.s - the sampled voice (bank 0): the timer interrupt writes one 3 bit sample to the master volume
; (NR50) every ~122 us.  The samples are packed two per byte, high nibble first, in one ROM bank.
	.module	snd_a
	.globl	_tim_isr, _pcm_ptr, _pcm_cnt, _pcm_bnk, _pcm_ph, _pcm_on, _cur_rom_bank

NR50 = 0x24
TAC  = 0x07
MBC  = 0x2000

	.area	_DATA
_pcm_ptr:	.ds	2
_pcm_cnt:	.ds	2		; bytes left
_pcm_bnk:	.ds	1
_pcm_ph:	.ds	1		; 0: the high nibble is next
_pcm_on:	.ds	1		; 0 off, 1 playing, 2 done (snd_frame puts the sound chip back)

	.area	_CODE
_tim_isr::
	ld	a,(#_pcm_on)
	dec	a
	ret	nz
	ld	a,(#_pcm_bnk)
	ld	(#MBC),a
	ld	hl,#_pcm_ptr
	ld	a,(hl+)
	ld	h,(hl)
	ld	l,a
	ld	a,(#_pcm_ph)
	or	a,a
	ld	a,(hl)
	jr	nz,1$
	swap	a			; high nibble
	and	a,#7
	ld	b,a
	swap	a
	or	a,b
	ldh	(NR50),a
	ld	a,#1
	ld	(#_pcm_ph),a
	jr	9$
1$:	and	a,#7			; low nibble
	ld	b,a
	swap	a
	or	a,b
	ldh	(NR50),a
	xor	a,a
	ld	(#_pcm_ph),a
	inc	hl
	ld	a,l
	ld	(#_pcm_ptr),a
	ld	a,h
	ld	(#_pcm_ptr+1),a
	ld	hl,#_pcm_cnt
	ld	a,(hl)
	sub	a,#1
	ld	(hl+),a
	ld	a,(hl)
	sbc	a,#0
	ld	(hl),a
	dec	hl
	or	a,(hl)
	jr	nz,9$
	ld	a,#2
	ld	(#_pcm_on),a		; the last sample is out
	xor	a,a
	ldh	(TAC),a
9$:	ld	a,(#_cur_rom_bank)
	ld	(#MBC),a
	ret
