/* render.h - the 3-D view: ray casting into a tile frame buffer, sprites, the HUD palette split */
#ifndef RENDER_H
#define RENDER_H
#include "hw.h"
#include "assets.h"

#define MAXVISIBLE  32

/* VRAM layout: BG tiles 0..239 of VRAM bank 0 / bank 1 are the two view buffers (column major: tile = tx * 12 + ty);
 * the maps at 9800 / 9C00 show one of them.  Tiles 240.. of bank 0 are the HUD. */
#define VIEW_TILES   (VIEW_TW * VIEW_TH)
#define HUD_T0       240
#define HUD_ROW0     VIEW_TH            /* first map row of the HUD */
#define HUD_ROWS     6

typedef struct {
    s16 cx;             /* centre pixel */
    u8  size;           /* height in pixels */
    u16 shape;          /* sprite number */
    u16 depth;          /* Q8 tiles */
} VisSprite;

extern u16 r_viewx, r_viewy;    /* camera position Q8 tiles */
extern u8  render_cast_dirty;   /* set when the map changes (kept for the game code) */
extern u8  cr_pwpos;            /* moving push wall: offset 0..63 (1/64 tile) */
extern u16 ray_t[NRAYS];        /* distance of each ray's wall (Q8 tiles), the z buffer */
extern VisSprite vissprites[MAXVISIBLE];
extern u8  numvis;
extern u8  view_attr[VIEW_TH * 32];   /* 0xC0A0 (cartsyms.s): a DMA source, 16 byte aligned */

void render_init(void) __banked;                 /* maps of both buffers (LCD off) */
void render_level_palettes(u8 level) __banked;   /* the wall palettes of a level */
void render_present(void);             /* queue the frame buffer for the next VBlanks */
void ThreeDRefresh(void) __banked;

#endif
