/* weapon.c - the player's weapon: up to 40 hardware sprites (8x8, 1:1) at the bottom of the view; the tiles of the
 * current frame are streamed into the OBJ tile slots 240..255 of both VRAM banks (tools/gb_hud.py). */
#include "game/def.h"

static u8 wp_weapon = 0xFF, wp_frame = 0xFF;

void weapon_load(void) B
{
    wp_weapon = wp_frame = 0xFF;
    oam_clear();
}

void weapon_hide(void) B
{
    wp_weapon = wp_frame = 0xFF;
    oam_clear();
}

void weapon_show(u8 weapon, u8 frame) B
{
    u8 hdr[3], i, n;
    u16 off;
    if (weapon > 3 || frame > 4)
        return;
    if (weapon == wp_weapon && frame == wp_frame)
        return;
    if (weapon != wp_weapon)
    {
        const u16 *p = wpn_pal + weapon * 32;
        for (i = 0; i < 32; i++)
            ((u16 *)pal_obj)[i] = p[i];
        pal_commit();
    }
    wp_weapon = weapon;
    wp_frame = frame;
    off = wpn_off[weapon * 5 + frame];
    rom_read(WPN_BANK, off, hdr, 3);
    n = hdr[0];
    {
        u16 t = off + ((3 + n * 4 + 15) & ~15);  /* the tile data follows the header, 16 byte aligned */
        if (hdr[1])
            dma_queue((const void *)t, WPN_BANK, 0, 0, 0x8000 + 240 * 16, hdr[1] * 16);
        if (hdr[2])
            dma_queue((const void *)(t + hdr[1] * 16), WPN_BANK, 0, 1, 0x8000 + 240 * 16, hdr[2] * 16);
    }
    rom_read(WPN_BANK, off + 3, oam_buf, (u16)n * 4);
    for (i = n; i < 40; i++)
        oam_buf[i].y = 0;
    if (rLCDC & LCDC_ON)
        oam_queue();                            /* the new layout goes out right after its tiles */
    else
        oam_set_dirty();
}
