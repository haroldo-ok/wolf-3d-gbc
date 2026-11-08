/* ui.h - the front end: text screens built from font tiles, pictures, menus, intermission, scores (banked) */
#ifndef UI_H
#define UI_H
#include "game/def.h"

/* tiles of the text screens: 0..94 the font (ASCII 32..126), 95..97 solid colour 1..3 */
#define T_SOLID1 95
#define T_SOLID2 96
#define T_SOLID3 97
#define T_FREE   104                    /* first tile slot for pictures (slots 0..255 bank 0, 256.. bank 1) */

/* palettes of the text screens (see tools/gb_ui.py) */
#define P_TEXT  0
#define P_HI    1
#define P_YEL   2
#define P_DIM   3
#define P_OUT   4                       /* on the outer background */
#define P_OUTY  5

#define UK_MENU  0
#define UK_INTER 1

/* engine (ui.c) */
void scr_open(u8 kind) B;               /* display off, empty screen, palettes of the kind (black) */
void scr_show(void) B;                  /* display on, fade in */
void scr_leave(void) B;                 /* fade out */
void ui_put(u8 col, u8 row, u8 tile, u8 attr) B;
void ui_text(u8 col, u8 row, const char *s, u8 pal) B;
void ui_ctext(u8 row, const char *s, u8 pal) B;                  /* centered on the 20 columns */
void ui_fill(u8 col, u8 row, u8 w, u8 h, u8 tile, u8 pal) B;
void ui_box(u8 col, u8 row, u8 w, u8 h) B;                       /* window: dark inside, a frame of solid tiles */
void ui_pal(u8 col, u8 row, u8 w, u8 pal) B;                     /* palette of the cells of a row (keeps tiles) */
void ui_num(u8 col, u8 row, u8 w, s32 v) B;                      /* right aligned decimal in w cells */
void ui_time(u8 col, u8 row, u16 min, u8 sec) B;
u16  pic_load(u8 id, u8 col, u8 row, u16 slot) B;                /* returns the next free slot */
void ui_cursor(u8 x, u8 y, u8 frame) B;                          /* pixels, frame 0/1 */
void ui_cursor_off(void) B;
void ui_scroll(u8 x, u8 y) B;
void ui_itoa(char *buf, s32 v) B;
void ui_pad(char *buf, s32 v, u8 w) B;
u8   ui_wait(u16 tics) B;               /* TRUE: a button was pressed before the time was up */
extern u8 ui_kind;

/* menus (menu.c) */
typedef struct { u8 col, row, flags; const char *text; } MenuItem;   /* flags 1: disabled, 2: yellow */
s8 menu_run(const MenuItem *it, u8 n, u8 start, void (*onmove)(u8)) B;   /* the chosen item, -1: back */
void HelpScreens(void) B;
void EndText(void) B;

/* save.c: 3 slots and the score table in cartridge RAM banks 2..5 */
typedef struct { u8 floor, diff, lives; s32 score; } SaveInfo;
u8 Save_Info(u8 slot, SaveInfo *si) B;  /* TRUE: the slot holds a game */
u8 Save_Write(u8 slot) B;
u8 Save_Read(u8 slot) B;
void Scores_Load(void) B;
void Scores_Store(void) B;

/* inter.c */
extern u8 ratio_kill[8], ratio_secret[8], ratio_treasure[8];
extern u16 ratio_time[8];
void title_loop(void) B;                /* the attract loop and the menu; returns when a game starts */
void Quit_ToTitle(void) B;
void Scores_Defaults(void) B;
void RestoreGameScreen(void) B;
#endif
