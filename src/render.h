/* render.h - the 3-D view: ray casting into a tile frame buffer, sprites, the HUD palette split */
#ifndef RENDER_H
#define RENDER_H
#include "hw.h"
#include "assets.h"

#define MAXVISIBLE  32

/* The view is drawn with the signed BG tile addressing (LCDC bit 4 clear): the view buffer of each VRAM bank is the
 * 240 tiles at 0x8800..0x96FF (tile index i is the map number (i + 128) & 255), which leaves 0x8000..0x87FF to the
 * sprites (OBJ tiles always start at 0x8000).  The HUD, below line 96, uses the unsigned addressing (the line
 * interrupt sets bit 4) and its tiles are at 0x8000 + 16 n of bank 0.  The sprite tiles of bank 0:
 *     0 .. WPN_T0-1       the HUD (88 tiles, tools/gb_hud.py)
 *     WPN_T0 ..  +32      the weapon (src/weapon.c)
 * and bank 1 holds the enemies (src/rspr.c): two pools of 64 tiles, one per view buffer, so that the tiles shown are
 * never overwritten: pool 0 = tiles 0..63, pool 1 = tiles 64..127.  The OBJ size is 8x16. */
#define VIEW_VRAM    0x8800u
#define FB           0xD000             /* the frame buffer (WRAM bank 3): column major tiles, tile column = 192 bytes */
#define COLBYTES     (VIEW_TH * 16)
#define FB_SPAN_TOP  ((u8 *)0xDF00)     /* in the frame buffer's bank: the rows drawn in each tile column (cast_a.s) */
#define FB_SPAN_BOT  ((u8 *)0xDF20)

/* The two view buffers are the tiles of VRAM bank 0 / bank 1 (column major: tile = tx * 12 + ty); the maps at
 * 9800 / 9C00 show one of them. */
#define VIEW_TILES   (VIEW_TW * VIEW_TH)
#define HUD_T0       240
#define HUD_ROW0     VIEW_TH            /* first map row of the HUD */
#define HUD_ROWS     6

#define VS_ACTOR     0x8000             /* in VisSprite.shape: an actor (an enemy or its shot), not a static object */
#define VS_HW        0x4000             /* ... it is drawn as hardware sprites (rspr.c decides) */
#define VS_SHAPE     0x3FFF

typedef struct {
    s16 cx;             /* centre pixel */
    u8  size;           /* height in pixels */
    u16 shape;          /* sprite number | VS_ACTOR | VS_HW */
    u16 depth;          /* Q8 tiles */
} VisSprite;

extern u16 r_viewx, r_viewy;    /* camera position Q8 tiles */
extern u8  render_cast_dirty;   /* set when the map changes (kept for the game code) */
extern u8  cr_pwpos;            /* moving push wall: offset 0..63 (1/64 tile) */
extern u16 ray_t[NRAYS];        /* distance of each ray's wall (Q8 tiles), the z buffer */
extern VisSprite vissprites[MAXVISIBLE];
extern u8  numvis;
extern u8  view_attr[VIEW_TH * 32];   /* 0xC0A0 (cartsyms.s): a DMA source, 16 byte aligned */

/* rspr.c (bank 8): the sprites of the view */
void spr_reset(void) __banked;                   /* the palette slots are empty */
void spr_slots(void) __banked;                   /* choose the sprite palettes of the visible sprites */
void spr_plan(void) __banked;                    /* the weapon's OAM entries, then which enemies are hardware sprites */
void spr_draw_sw(u8 bankbit) __banked;           /* the other sprites, into the frame buffer */
void spr_draw_hw(u8 back) __banked;              /* the enemies: tiles into the staging buffer, entries into the OAM */
void spr_queue(u8 back) __banked;                /* the copies that go with the frame: enemy tiles, weapon tiles */
extern u8 hw_n;                                  /* OAM entries of the enemies in this frame */

void render_init(void) __banked;                 /* maps of both buffers (LCD off) */
void render_level_palettes(u8 level) __banked;   /* the wall palettes of a level */
void ThreeDRefresh(void) __banked;

#endif
