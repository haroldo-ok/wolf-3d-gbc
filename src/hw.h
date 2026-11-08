/* hw.h - Game Boy Color hardware layer used by the game code (see gbc.h for the registers) */
#ifndef HW_H
#define HW_H
#include "gbc.h"

/* pad: the game's three buttons are A (fire), B (use / strafe) and START (pause, the menu); SELECT changes weapon */
#define PAD_UP     K_UP
#define PAD_DOWN   K_DOWN
#define PAD_LEFT   K_LEFT
#define PAD_RIGHT  K_RIGHT
#define PAD_1      K_A
#define PAD_2      K_B
#define PAD_PAUSE  K_START
#define PAD_SEL    K_SELECT

extern volatile u32 time_count;      /* 70 Hz game clock */
extern volatile u8 frame_count;
extern u8  pad_new;
extern u16 pad_f_up, pad_f_down, pad_f_left, pad_f_right;
extern u8 pad_acc[4], pad_nacc, pad_or;      /* the frame interrupt's pad samples (hw.c) */

void hw_init(void) __banked;
void wait_vblank(void);
void wait_frames(u16 n);
u8   pad_read(void);
void rom_read(u8 bank, u16 addr, void *dst, u16 len);
u8   tm_get(u8 x, u8 y);
void tm_set(u8 x, u8 y, u8 v);

/* ---- LCD / VRAM / palettes (hw.c) */
#define VMODE_UI   0                 /* menus: no splits */
#define VMODE_VIEW 1                 /* 3-D view: ceiling / floor and HUD palette swaps by line */
extern u8 vmode;
extern u16 pal_bg[8][4], pal_obj[8][4];      /* the palettes as the game set them (rgb555) */
extern u16 pal_ceil, pal_floor;              /* view mode: colour 0 of the palettes above / below the horizon */
extern u16 pal_hud[8][4];                    /* view mode: all eight palettes below the view (P6/P7 from line 96, the rest from line 98) */
extern u8  pal_fade;                         /* 0 normal .. FADE_MAX black */
#define FADE_MAX 8
extern u16 pal_fade_to;                      /* the colour a fade goes to (0 black) */
extern u8  pal_tint_r, pal_tint_g, pal_tint_b, pal_tint_lv;
void pal_commit(void) __banked;                       /* recompute the live palettes and upload them at the next VBlank */
void lcd_off(void);
void lcd_on(u8 lcdc);
void lcd_wait_commit(void);
u16  rgb555(u8 r, u8 g, u8 b);

/* GDMA queue: copies done at the start of VBlank; src is in WRAM0, a WRAM bank (swram != 0) or a ROM bank (srom != 0xFF) */
void dma_queue(const void *src, u8 srom, u8 swram, u8 vbank, u16 dst, u16 len);
void dma_wait(void);
void oam_queue(void);                   /* the sprite copy, in order after the jobs queued before */
extern volatile u8 dma_busy;
void flip_queue(void);                       /* after the queued copies: show the other BG map */
extern u8 map_front;                         /* 0: 9800 shown, 1: 9C00 */
void vram_set(u8 vbank, u16 dst, const u8 *src, u16 len);     /* LCD off or in VBlank only */
void vram_fill8(u8 vbank, u16 dst, u8 v, u16 len);
void rom_to_vram(u8 rombank, const u8 *src, u8 vbank, u16 dst, u16 len);   /* LCD off: through a bank switch */

/* OAM */
typedef struct { u8 y, x, tile, attr; } OamEntry;
extern OamEntry oam_buf[40];      /* 0xC000 (hw_a.s) */
void oam_clear(void);
void oam_set_dirty(void);

/* split timing data for the view mode */
extern u8 cur_lcdc;
#endif
