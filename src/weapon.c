/* weapon.c - the player's weapon: up to 16 hardware sprites (8x16, 1:1) at the bottom of the view; the tiles of the
 * current frame are streamed into the OBJ tiles WPN_T0.. of VRAM bank 0 (tools/gb_hud.py).
 *
 * The OAM is shared with the enemies (render.c) and is composed once per frame: weapon_show only notes what the
 * weapon looks like, weapon_oam puts its entries first in the OAM buffer (they are in front of the enemies, and the
 * hardware drops the entries after the first 10 of a line), and weapon_flush queues the tile copy, which must
 * reach VRAM together with the OAM and the view flip. */
#include "game/def.h"

static u8 wp_weapon = 0xFF, wp_frame = 0xFF;
static u8 wp_dma;                       /* the tiles of wp_frame are not in VRAM yet */
static u16 wp_off;                      /* the frame's blob (WPN_BANK) */

void weapon_load(void) B
{
    wp_weapon = wp_frame = 0xFF;
    wp_dma = 0;
    oam_clear();
}

void weapon_hide(void) B
{
    wp_weapon = wp_frame = 0xFF;
    wp_dma = 0;
    oam_clear();
}

void weapon_show(u8 weapon, u8 frame) B
{
    u8 i;
    if (weapon > 3 || frame > 4)
        return;
    if (weapon == wp_weapon && frame == wp_frame)
        return;
    if (weapon != wp_weapon)
    {
        const u16 *p = wpn_pal + weapon * 32;
        for (i = 0; i < WPN_NPAL * 4; i++)      /* (OBJ palettes 6 and 7 are the enemies') */
            ((u16 *)pal_obj)[i] = p[i];
        pal_commit();
    }
    wp_weapon = weapon;
    wp_frame = frame;
    wp_off = wpn_off[weapon * 5 + frame];
    wp_dma = 1;
}

/* the weapon's entries at the start of the OAM buffer; returns how many */
u8 weapon_oam(void) B
{
    u8 hdr[2];
    if (wp_weapon == 0xFF)
        return 0;
    rom_read(WPN_BANK, wp_off, hdr, 2);
    rom_read(WPN_BANK, wp_off + 2, oam_buf, (u16)hdr[0] * 4);
    return hdr[0];
}

/* the copy of the frame's tiles (queued before the sprites and the flip of the frame) */
void weapon_flush(void) B
{
    u8 hdr[2];
    if (!wp_dma)
        return;
    wp_dma = 0;
    rom_read(WPN_BANK, wp_off, hdr, 2);
    if (hdr[1])
    {
        u16 t = wp_off + ((2 + hdr[0] * 4 + 15) & ~15);  /* the tile data follows the header, 16 byte aligned */
        if (rLCDC & LCDC_ON)
            dma_queue((const void *)t, WPN_BANK, 0, 0, 0x8000 + WPN_T0 * 16, hdr[1] * 16);
        else
            rom_to_vram(WPN_BANK, (const u8 *)t, 0, 0x8000 + WPN_T0 * 16, hdr[1] * 16);
    }
}
