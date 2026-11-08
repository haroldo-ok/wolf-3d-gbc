/* vid.c - the play screen set-up, palette fades and flashes, the fizzle fade (banked, with render.c / hud.c / weapon.c) */
#include "game/def.h"

u8 screenfaded;
/* palette fades and tints (moved out of the fixed bank) */
extern u16 live_bg[8][4], live_obj[8][4], live_floor, live_hud[32];
extern volatile u8 pal_pending;
extern void pal_up_bg(void), pal_up_obj(void);

static u16 fx(u16 c)
{
    s16 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    if (pal_tint_lv)
    {
        if (pal_tint_r) r += ((31 - r) * pal_tint_lv) >> 3;
        if (pal_tint_g) g += ((31 - g) * pal_tint_lv) >> 3;
        if (pal_tint_b) b += ((31 - b) * pal_tint_lv) >> 3;
    }
    if (pal_fade)
    {
        s16 tr = pal_fade_to & 31, tg = (pal_fade_to >> 5) & 31, tb = (pal_fade_to >> 10) & 31;
        r += ((tr - r) * pal_fade) >> 3;
        g += ((tg - g) * pal_fade) >> 3;
        b += ((tb - b) * pal_fade) >> 3;
    }
    return (u16)(r | (g << 5) | (b << 10));
}

void pal_commit(void) B
{
    u8 p, i;
    u8 on = (rLCDC & LCDC_ON) != 0;
    for (p = 0; p < 8; p++)
        for (i = 0; i < 4; i++)
        {
            live_bg[p][i] = fx(pal_bg[p][i]);
            live_obj[p][i] = fx(pal_obj[p][i]);
        }
    if (vmode == VMODE_VIEW)
    {
        u16 c = fx(pal_ceil);
        for (p = 0; p < 8; p++)
            live_bg[p][0] = c;
        live_floor = fx(pal_floor);
        for (i = 0; i < 32; i++)
            live_hud[i] = fx(pal_hud[i >> 2][i & 3]);
    }
    if (on)
        pal_pending = 3;
    else
    {
        pal_up_bg();
        pal_up_obj();
    }
}


static u8 flash_cur;

void vid_set_palette_black(void) B
{
    pal_fade = FADE_MAX;
    pal_fade_to = 0;
    pal_commit();
    screenfaded = 1;
}

void vid_set_palette_normal(void) B
{
    pal_fade = 0;
    pal_commit();
    screenfaded = 0;
}

void vid_wait(u16 frames) B
{
    while (frames--)
        wait_vblank();
}

void vid_fade_out(void) B
{
    u8 k;
    if (screenfaded)
        return;
    pal_fade_to = 0;
    for (k = 1; k <= FADE_MAX; k++)
    {
        pal_fade = k;
        pal_commit();
        vid_wait(2);
    }
    screenfaded = 1;
}

void vid_fade_in(void) B
{
    u8 k;
    pal_fade_to = 0;
    for (k = FADE_MAX; k-- > 0;)
    {
        pal_fade = k;
        pal_commit();
        vid_wait(2);
    }
    screenfaded = 0;
}

/* the colour flashes of the original (damage red, pickup gold): level 0 = normal */
void vid_flash(u8 r, u8 g, u8 b, u8 level) B
{
    u8 key = level ? (u8)(r | (g << 1) | (b << 2) | (level << 3)) : 0;
    if (key == flash_cur || screenfaded)
        return;
    flash_cur = key;
    pal_tint_r = r;
    pal_tint_g = g;
    pal_tint_b = b;
    pal_tint_lv = level * 2;
    pal_commit();
}

void vid_set_ceiling(u8 map_ceil) B
{
    (void)map_ceil;
}

void VW_ColorBorder(u8 color) B
{
    (void)color;
}

/* the play screen: the view, the HUD and the weapon; the display is on afterwards, palettes as they were */
void vid_init_game(void) B
{
    lcd_off();
    vmode = VMODE_VIEW;
    flash_cur = 0;
    pal_tint_lv = 0;
    render_init();
    render_level_palettes(gamestate.mapon);
    hud_load();
    weapon_load();
    oam_clear();
    pal_commit();
    lcd_on(LCDC_BGON | LCDC_OBJON | LCDC_BGDATA8000);
}

/* the fizzle: the new view appears cell by cell in pseudo random order */
void fizzle_begin(void) B
{
}

void FizzleFade(void) B
{
}

/* the quarter squares of umul16 (mathz_a.s): f(n) = n * n / 4, n < 512, in WRAM bank 5 */
void mul_init(void) __banked
{
    u16 n;
    u32 sq = 0;
    u8 old = rSVBK;
    rSVBK = 5;
    for (n = 0; n < 512; n++)
    {
        u16 f = (u16)(sq >> 2);
        ((u8 *)0xD000)[n] = (u8)f;
        ((u8 *)0xD200)[n] = (u8)(f >> 8);
        sq += 2 * (u32)n + 1;
    }
    rSVBK = old;
}

/* power on (moved out of bank 0, which is full) */
void oam_dma_install(void);
void hw_init(void) __banked
{
    u8 i;
    mRAMG = 0x0A;
    ram_bank(0);
    wram_bank(1);
    oam_dma_install();
    rLCDC = 0;
    rSTAT = 0;
    rIF = 0;
    rSCX = rSCY = 0;
    rWX = 7;
    rWY = 144;
    rNR52 = 0x80;
    rNR50 = 0x77;
    rNR51 = 0xFF;
    for (i = 0; i < 40; i++) { oam_buf[i].y = 0; oam_buf[i].x = 0; oam_buf[i].tile = 0; oam_buf[i].attr = 0; }
    cur_lcdc = LCDC_BGDATA8000;
    rIE = IE_VBLANK | IE_STAT;
    ei();
}
