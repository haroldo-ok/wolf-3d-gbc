;; cartsyms.s - arrays that live in cartridge RAM bank 0 (layout: cartram.h)
	.module cartsyms
	.globl  _statobjlist, _doorobjlist
_statobjlist = 0xB700
_doorobjlist = 0xBBB0
_tilemap = 0xD000
_actorat = 0xD000
	.globl	_tilemap, _actorat
; DMA sources must be 16 byte aligned: fixed buffers at the start of WRAM bank 0 (after the OAM buffer)
_view_attr = 0xC0A0
_hmap = 0xC220
_hatr = 0xC2E0
	.globl	_view_attr, _hmap, _hatr
