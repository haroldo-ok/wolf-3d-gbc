/* ui.c - text screens built from font tiles, pictures from the ROM, the cursor sprite (banked) */
#include "ui.h"
#include <string.h>

u8 ui_kind;
extern const u16 ui_pals[64];
extern const u16 ui_curs_pal[4];
extern const u8 pic_bank[NPICS];
extern const u16 pic_addr[NPICS];

#define MAP0 0x9800
static u8 lcd_is_on_(void) { return (rLCDC & LCDC_ON) != 0; }

/* VRAM is not reachable while the LCD draws a line (STAT mode 3).  A cell (tile and attribute) is written right
 * after the PPU is seen outside mode 3's lead-in (mode 0, 1): 8 instructions with interrupts off, so it is done well
 * inside the 80 dots of mode 2 that follow even in double speed. */
static void cell(volatile u8 *p, u8 tile, u8 attr)
{
    di();
    if (rLCDC & LCDC_ON)
        while (rSTAT & 2) ;
    rVBK = 1;
    *p = attr;
    rVBK = 0;
    *p = tile;
    ei();
}

static u8 attr_rd(volatile u8 *p)
{
    u8 v;
    di();
    if (rLCDC & LCDC_ON)
        while (rSTAT & 2) ;
    rVBK = 1;
    v = *p;
    rVBK = 0;
    ei();
    return v;
}

static void attr_wr(volatile u8 *p, u8 a)
{
    di();
    if (rLCDC & LCDC_ON)
        while (rSTAT & 2) ;
    rVBK = 1;
    *p = a;
    rVBK = 0;
    ei();
}

static void vb_wait(void)
{
    if (lcd_is_on_())
        wait_vblank();
}

/* ---------------------------------------------------------------- screens */
void scr_open(u8 kind) B
{
    u8 i;
    ui_kind = kind;
    lcd_off();
    vmode = VMODE_UI;
    rSCX = rSCY = 0;
    memcpy(pal_bg, ui_pals + kind * 32, 64);
    for (i = 0; i < 4; i++)
        pal_obj[0][i] = ui_curs_pal[i];
    rom_to_vram(UI_FONT_BANK, (const u8 *)UI_FONT_ADDR, 0, 0x8000, UI_FONT_TILES * 16);
    rom_to_vram(UI_CURS_BANK, (const u8 *)UI_CURS_ADDR, 0, 0x8000 + 240 * 16, 64);
    vram_fill8(0, MAP0, 0, 1024);
    vram_fill8(1, MAP0, P_OUT, 1024);
    oam_clear();
    vid_set_palette_black();
}

void scr_show(void) B
{
    lcd_on(LCDC_BGON | LCDC_OBJON | LCDC_BGDATA8000);
    vid_fade_in();
}

void scr_leave(void) B
{
    vid_fade_out();
    ui_cursor_off();
}

/* ---------------------------------------------------------------- cells */
void ui_put(u8 col, u8 row, u8 tile, u8 attr) B
{
    volatile u8 *p = (volatile u8 *)(MAP0 + ((u16)row << 5) + col);
    cell(p, tile, attr);
}

static void run(u8 col, u8 row, u8 n, const u8 *tiles, u8 pal)
{
    volatile u8 *p = (volatile u8 *)(MAP0 + ((u16)row << 5) + col);
    u8 i;
    vb_wait();
    for (i = 0; i < n; i++)
        cell(p + i, tiles[i], pal);
}

void ui_text(u8 col, u8 row, const char *s, u8 pal) B
{
    u8 t[24], n = 0, c;
    while (*s && n < 24)
    {
        c = (u8)*s++;
        t[n++] = (c < 32 || c > 126) ? 0x1F : (u8)(c - 32);
    }
    run(col, row, n, t, pal);
}

void ui_ctext(u8 row, const char *s, u8 pal) B
{
    u8 n = (u8)strlen(s);
    ui_text(n >= 20 ? 0 : (20 - n) >> 1, row, s, pal);
}

void ui_fill(u8 col, u8 row, u8 w, u8 h, u8 tile, u8 pal) B
{
    u8 x, y, t[20];
    for (x = 0; x < w; x++)
        t[x] = tile;
    for (y = 0; y < h; y++)
        run(col, row + y, w, t, pal);
}

void ui_box(u8 col, u8 row, u8 w, u8 h) B
{
    u8 x, t[20];
    ui_fill(col, row, w, h, 0, P_TEXT);
    for (x = 0; x < w; x++)
        t[x] = T_SOLID2;
    run(col, row, w, t, P_TEXT);
    for (x = 0; x < w; x++)
        t[x] = T_SOLID3;
    run(col, row + h - 1, w, t, P_TEXT);
    for (x = 1; x + 1 < h; x++)
    {
        ui_put(col, row + x, T_SOLID2, P_TEXT);
        ui_put(col + w - 1, row + x, T_SOLID3, P_TEXT);
    }
}

void ui_pal(u8 col, u8 row, u8 w, u8 pal) B
{
    volatile u8 *p = (volatile u8 *)(MAP0 + ((u16)row << 5) + col);
    u8 i, old[20];
    vb_wait();
    for (i = 0; i < w; i++)
    {
        old[i] = attr_rd(p + i);
        attr_wr(p + i, (old[i] & 0xF8) | pal);
    }
}

/* by subtraction of the powers of ten (the s32 division of the library would take room in the fixed bank) */
static const u32 pow10[10] = { 1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL, 10000UL, 1000UL, 100UL,
                               10UL, 1UL };
void ui_itoa(char *buf, s32 v) B
{
    u32 u;
    u8 k, i = 0, any = 0;
    if (v < 0) { buf[i++] = '-'; u = (u32)-v; } else u = (u32)v;
    for (k = 0; k < 10; k++)
    {
        char d = '0';
        while (u >= pow10[k]) { u -= pow10[k]; d++; }
        if (d != '0' || any || k == 9) { buf[i++] = d; any = 1; }
    }
    buf[i] = 0;
}

void ui_pad(char *buf, s32 v, u8 w) B
{
    char t[12];
    u8 n, i;
    ui_itoa(t, v);
    for (n = 0; t[n]; n++) ;
    for (i = 0; i < w; i++)
        buf[i] = i + n < w ? ' ' : t[i + n - w];
    buf[w] = 0;
}

void ui_num(u8 col, u8 row, u8 w, s32 v) B
{
    char b[12];
    ui_pad(b, v, w);
    ui_text(col, row, b, P_TEXT);
}

void ui_time(u8 col, u8 row, u16 min, u8 sec) B
{
    char b[6];
    b[0] = '0' + (min / 10) % 10;
    b[1] = '0' + min % 10;
    b[2] = ':';
    b[3] = '0' + sec / 10;
    b[4] = '0' + sec % 10;
    b[5] = 0;
    ui_text(col, row, b, P_TEXT);
}

u8 ui_wait(u16 tics) B
{
    u32 t0 = get_timecount();
    IN_ClearKeys();
    do
    {
        wait_vblank();
        if (IN_CheckAck())
            return 1;
    } while (get_timecount() - t0 < tics);
    return 0;
}

/* ---------------------------------------------------------------- pictures */
/* PIC blob: u8 w, h, npal, palbase; u16 ntiles; u16 pal[npal*4]; u16 map[w*h]; u8 attr[w*h]; pad to 16; tiles */
u16 pic_load(u8 id, u8 col, u8 row, u16 slot) B
{
    u8 bank = pic_bank[id];
    u16 a = pic_addr[id], nt, tofs, s, k, run_n, t;
    u8 hdr[6], w, h, x, y, on = lcd_is_on_();
    u16 mp[32];
    u8 at[32];
    rom_read(bank, a, hdr, 6);
    w = hdr[0];
    h = hdr[1];
    nt = hdr[4] | ((u16)hdr[5] << 8);
    rom_read(bank, a + 6, &pal_bg[hdr[3]][0], hdr[2] * 8);
    tofs = (6 + hdr[2] * 8 + (u16)w * h * 3 + 15) & ~15;
    /* the tiles: slots 0..255 are bank 0, 256.. bank 1 */
    for (s = slot, k = 0; k < nt; )
    {
        u8 vb = s >= 256;
        u16 base = vb ? s - 256 : s;
        run_n = nt - k;
        if (!vb && s + run_n > 256)
            run_n = 256 - s;
        if (on)
            dma_queue((const void *)(a + tofs + k * 16), bank, 0, vb, 0x8000 + base * 16, run_n * 16);
        else
            rom_to_vram(bank, (const u8 *)(a + tofs + k * 16), vb, 0x8000 + base * 16, run_n * 16);
        s += run_n;
        k += run_n;
    }
    if (on)
    {
        dma_wait();
        pal_commit();
    }
    for (y = 0; y < h; y++)
    {
        volatile u8 *p = (volatile u8 *)(MAP0 + ((u16)(row + y) << 5) + col);
        rom_read(bank, a + 6 + hdr[2] * 8 + (u16)y * w * 2, mp, w * 2);
        rom_read(bank, a + 6 + hdr[2] * 8 + (u16)w * h * 2 + (u16)y * w, at, w);
        vb_wait();
        for (x = 0; x < w; x++)
        {
            t = slot + mp[x];
            cell(p + x, (u8)(t >= 256 ? t - 256 : t), (at[x] & 0x60) | (u8)((at[x] & 7) + hdr[3]) | (t >= 256 ? 0x08 : 0));
        }
    }
    return slot + nt;
}

/* ---------------------------------------------------------------- cursor */
void ui_cursor(u8 x, u8 y, u8 frame) B
{
    oam_buf[0].y = y + 16;
    oam_buf[0].x = x + 8;
    oam_buf[0].tile = 240 + frame * 2;
    oam_buf[0].attr = 0;
    oam_buf[1].y = y + 16;
    oam_buf[1].x = x + 16;
    oam_buf[1].tile = 241 + frame * 2;
    oam_buf[1].attr = 0;
    oam_set_dirty();
}

void ui_cursor_off(void) B
{
    oam_buf[0].y = 0;
    oam_buf[1].y = 0;
    oam_set_dirty();
}

void ui_scroll(u8 x, u8 y) B
{
    wait_vblank();
    rSCX = x;
    rSCY = y;
}
