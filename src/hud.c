/* hud.c - the status panel under the view: 20 x 6 cells at map rows 12..17 (see tools/gb_hud.py).
 * The map and attribute images live in WRAM and are copied to both background maps in VBlank. */
#include "game/def.h"

extern u8 hmap[192], hatr[192];      /* cartsyms.s: DMA sources, 16 byte aligned */
static u8 hdirty;
static u8 cur_face = 0xFF, cur_icon = 0xFF;

#define HUD_VRAM 0x9000u

static u8 lcd_is_on(void) { return (rLCDC & LCDC_ON) != 0; }

static void push(void)
{
    if (lcd_is_on())
    {
        dma_queue(hmap, 0xFF, 0, 0, 0x9980, 192);
        dma_queue(hmap, 0xFF, 0, 0, 0x9D80, 192);
        dma_queue(hatr, 0xFF, 0, 1, 0x9980, 192);
        dma_queue(hatr, 0xFF, 0, 1, 0x9D80, 192);
    }
    else
    {
        vram_set(0, 0x9980, hmap, 192);
        vram_set(0, 0x9D80, hmap, 192);
        vram_set(1, 0x9980, hatr, 192);
        vram_set(1, 0x9D80, hatr, 192);
    }
    hdirty = 0;
}

static void tiles_to(u8 rombank, u16 addr, u16 tile, u16 len)
{
    if (lcd_is_on())
        dma_queue((const void *)addr, rombank, 0, 0, HUD_VRAM + tile * 16, len);
    else
        rom_to_vram(rombank, (const u8 *)addr, 0, HUD_VRAM + tile * 16, len);
}

void hud_load(void) B
{
    u8 i;
    for (i = 0; i < 32; i++)
        ((u16 *)pal_hud)[i] = hud_pal[i];
    rom_to_vram(HUD_BANK, (const u8 *)HUD_STATIC_ADDR, 0, HUD_VRAM, HUD_STATIC_N * 16);
    rom_read(HUD_BANK, HUD_MAP_ADDR, hmap, 192);
    rom_read(HUD_BANK, HUD_MAP_ADDR + 192, hatr, 192);
    cur_face = cur_icon = 0xFF;
    push();
}

void hud_number(u8 which, s32 value) B
{
    static const u8 pos[5][3] = { { 0, 1, 2 }, { 2, 1, 6 }, { 7, 3, 1 }, { 12, 1, 3 }, { 16, 1, 2 } };
    static const u32 pw[6] = { 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
    u8 c0 = pos[which][0], r0 = pos[which][1], n = pos[which][2], k, d, started = 0;
    u32 v = value < 0 ? 0 : (u32)value;
    for (k = 0; k < n; k++)
    {
        u32 p = pw[6 - n + k];
        d = 0;
        while (v >= p)
        {
            v -= p;
            d++;
        }
        if (d == 0 && !started && k != n - 1)
            d = 10;
        else
            started = 1;
        hmap[r0 * 32 + c0 + k] = hud_dig[2 * d];
        hmap[(r0 + 1) * 32 + c0 + k] = hud_dig[2 * d + 1];
    }
    hdirty = 1;
}

void hud_face(u8 pic) B
{
    u8 a[16], i, j;
    if (pic == cur_face)
        return;
    cur_face = pic;
    tiles_to(HUD_FACE_BANK, HUD_FACE_ADDR + (u16)pic * 256, HUD_FACE0, 256);
    rom_read(HUD_FACE_BANK, HUD_FACE_ATTR_ADDR + (u16)pic * 16, a, 16);
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
            hatr[(1 + j) * 32 + 8 + i] = a[j * 4 + i];
    hdirty = 1;
}

void hud_weapon(u8 weapon) B
{
    u8 a[18], i, j;
    if (weapon > 3 || weapon == cur_icon)
        return;
    cur_icon = weapon;
    tiles_to(HUD_FACE_BANK == HUD_ICON_BANK ? HUD_ICON_BANK : HUD_ICON_BANK, HUD_ICON_ADDR + (u16)weapon * 288, HUD_ICON0, 288);
    rom_read(HUD_ICON_BANK, HUD_ICON_ATTR_ADDR + (u16)weapon * 18, a, 18);
    for (j = 0; j < 3; j++)
        for (i = 0; i < 6; i++)
            hatr[(3 + j) * 32 + i] = a[j * 6 + i];
    hdirty = 1;
}

void hud_keys(u8 keys) B
{
    u8 k, w;
    for (k = 0; k < 2; k++)
    {
        u8 c = k ? 14 : 12;
        w = (keys & (1 << k)) ? 1 + k : 0;
        hmap[3 * 32 + c] = hud_key[w * 3];
        hmap[4 * 32 + c] = hud_key[w * 3 + 1];
        hatr[3 * 32 + c] = hatr[4 * 32 + c] = hud_key[w * 3 + 2];
    }
    hdirty = 1;
}

void hud_messages(void) B
{
}

void hud_flush(void) B
{
    if (hdirty)
        push();
}
