/* cartram.h - memory map of the Game Boy Color port
 *
 *  C000-CFFF  WRAM0   globals, stack (top), statics (CR_STATLIST), doors, caster tables
 *  D000-DFFF  WRAMX   bank 1: actorat (default)   bank 2: tilemap   bank 3: frame buffer   bank 4: save buffer
 *                     bank 5: multiply tables   bank 6: sprite staging, OAM copies   bank 7: the second frame buffer
 *  A000-BFFF  SRAM    bank 0: objlist + scratch      banks 2..: saves and high scores (bank 1: music work)
 */
#ifndef CARTRAM_H
#define CARTRAM_H

#define MAPSIZE   64

#define WB_ACTORAT 1
#define WB_TILEMAP 2
#define WB_FRAME   3
#define WB_SAVE    4
#define WB_SPRITES 6             /* the enemies' tile staging (0xD000, 0xD400) and the OAM copies (0xDC00, 0xDD00) */
#define WB_FRAME2  7             /* the second frame buffer */

#define CR_TILEMAP   0xD000      /* u8 [64][64], x major (WRAM bank 2): 0 floor, 1..63 wall (|0x40 beside a door),
                                    0x80|n door n, 0xC0|n moving push wall */
#define CR_ACTORAT   0xD000      /* u8 [64][64] (WRAM bank 1): 0 nothing, 1..63 wall/blocker, 0x80|n door, 0x40|id actor */
#define CR_OBJLIST   0xA000      /* objtype [MAXACTORS] (SRAM bank 0) */
#define CR_OBJSIZE   50
#define CR_DOORPOS   doorpos8
#define CR_DOORPAGE  doorpage8
#define CR_STATLIST  0xB700      /* statobj_t [MAXSTATS] */
#define CR_DOORLIST  0xBBB0      /* doorobj_t [MAXDOORS] */
#define CR_SCRATCH   0xBDF0      /* 528 bytes of work space */
#define CR_SCRATCH_SIZE 0x210
#define CR_END       0xC000

/* tilemap accesses must be bracketed by TM_ON / TM_OFF (the caster and CheckLine map the bank for their whole run) */
#define TM_ON()   u8 tm_old__ = rSVBK; rSVBK = WB_TILEMAP
#define TM_OFF()  rSVBK = tm_old__

extern u8 doorpos8[64], doorpage8[64], doorvert8[64];   /* level.c: door position / 256, texture, vertical flag (the caster reads them) */

#endif
