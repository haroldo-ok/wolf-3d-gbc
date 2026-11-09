/* rspr.c - the sprites of the 3-D view (bank 8, with their tables, gen/assets_p.c).
 *
 * The statics (items, lamps, ...) and the enemies that do not fit the hardware are scaled into the frame buffer
 * like the walls (spr_draw_sw).  The enemies (the actors) that fit are hardware sprites (spr_plan decides, spr_draw_hw
 * draws): each is scaled by the same column routines into its own tile buffer (WRAM bank 6), which is copied with the
 * view buffer into one of two pools of OBJ tiles in VRAM bank 1; the OAM entries (8x16, one palette each) go after
 * the weapon's.  What that buys: the enemy does not give its colours to the wall tiles it stands in front of (a BG
 * tile has one palette), and the frame buffer has less to draw.  What it costs: the hardware shows at most 40
 * sprites and 10 on a line, the weapon takes up to 16 entries, and an 8x16 sprite is a block of tiles, so the nearer
 * an enemy is the more entries it needs; the ones that do not fit stay in the view buffer.
 *
 * A hardware sprite is always above the view buffer, so an enemy goes to the buffer too when a nearer sprite that
 * is drawn there overlaps it; the walls in front of an enemy cut its columns (as in the buffer), and among the
 * hardware sprites the OAM order is the depth order (the nearest first). */
#include "game/def.h"
#include <string.h>

extern u8  dc_bank;
extern u16 dc_fb, dc_sc, dc_s;
void spr_left(void);
void spr_right(void);
u16 umulq8(u16 t, u16 r);
#define mulsat8(a, b) umulq8((a), (b))      /* (a * b) >> 8, a <= 256 */
u8 pair_empty(u16 p);                       /* render_a.s: the 32 bytes at p are 0 */
void clr32(u16 p, u16 n);                   /* render_a.s: clear n blocks of 32 bytes */
/* the offset of texture column c of a sprite: 128 bytes a column (no u16 shift loop) */
#define COLOFS(c)    (((u16)((u8)(c) >> 1) << 8) | (((u8)(c) & 1) ? 0x80 : 0))

#define HW_BANK      WB_SPRITES             /* WRAM bank of the staging buffers */
#define HW_STAGE(p)  ((p) ? 0xD400 : 0xD000)   /* one for each parity of the frames (two can be in the DMA queue) */
#define HW_PAIRS     32                     /* tile pairs (8x16 sprites) of a pool: 64 tiles */
#define HW_MAXH      96                     /* the tall scalers (nearer than that) are for the view buffer */
#define OAM_N        40
#define LINE_MAX     10                     /* sprites per line the hardware shows */

u8 hw_n;
static u8 hw_n0;                            /* the weapon's entries (they come first) */
static u16 hw_bytes;                        /* tile bytes in the staging buffer */

/* ------------------------------------------------------------ palettes */
/* the two palettes of the sprites: BG palettes 6 and 7 (for the view buffer) and the OBJ palettes 6 and 7 */
static u8 slot_pool[2] = { 0xFF, 0xFF };
static u8 spr_slot[MAXVISIBLE];

void spr_reset(void) B
{
    slot_pool[0] = slot_pool[1] = 0xFF;
}

void spr_slots(void) B
{
    u8 i, n = 0, np[2] = { 0xFF, 0xFF }, k, changed = 0;
    for (i = numvis; i-- > 0; )             /* the nearest sprite first */
    {
        u8 p = spr_pool[vissprites[i].shape & VS_SHAPE];
        if (np[0] == p) k = 0;
        else if (np[1] == p) k = 1;
        else if (n < 2) { np[n] = p; k = n; n++; }
        else k = 0;
        spr_slot[i] = k;
    }
    for (k = 0; k < 2; k++)
        if (np[k] != 0xFF && np[k] != slot_pool[k])
        {
            u8 j;
            slot_pool[k] = np[k];
            for (j = 0; j < 3; j++)
                pal_bg[6 + k][1 + j] = pal_obj[6 + k][1 + j] = pool_pal[np[k] * 3 + j];
            changed = 1;
        }
    if (changed)
        pal_commit();
}

/* ------------------------------------------------------------ geometry */
typedef struct {
    s16 xl;                 /* the left pixel of the sprite's square */
    s16 i0, i1;             /* the rays it spans */
    u16 step;               /* texture columns per pixel, Q8 */
    const u8 *sc;           /* the scaler (screen row -> texture row), from its first row with a texel row of the
                               sprite's drawn rows (spr_t0: the transparent rows above are not drawn) */
    u8 top, bot;            /* the screen row of that scaler entry, the row after its last one */
    u8 r0, r1;              /* the tile rows its texels reach (r0 > r1: none on the screen) */
} SprGeo;

static void scaler_for(u8 h, const u8 **sc, u8 *top)
{
    if (h <= 96)
    {
        *sc = scaler_data + scaler_small[h];
        *top = 48 - (h >> 1);
    }
    else
    {
        u8 k = (u8)((h - 96 + 4) >> 3);
        if (k < 1) k = 1;
        if (k > 20) k = 20;
        *sc = scaler_data + scaler_tall[k - 1];
        *top = 0;
    }
}

static u8 spr_geom(const VisSprite *vs, SprGeo *g)
{
    u8 h = vs->size;
    u16 shape = vs->shape & VS_SHAPE;
    s16 xl = vs->cx - (h >> 1);
    s16 i0 = xl >> 2, i1 = (xl + h - 1) >> 2, y0, y1;

    if (i0 < 0) i0 = 0;
    if (i1 > NRAYS - 1) i1 = NRAYS - 1;
    if (i1 < i0)                            /* off the view: the u8 loops would never end (i1 = 255) */
        return FALSE;
    scaler_for(h, &g->sc, &g->top);
    {
        /* the first scaler row that reaches texel row spr_t0: ceil(t0 * h' / 64) - s, h' the scaler's height and s
         * its first row (tall scalers: h' = 96 + 8k, s = 4k) */
        u8 rows = h < 96 ? h : 96, k0;
        u16 hh = h;
        u8 st = 0;
        if (h > 96)
        {
            u8 k = (u8)((h - 96 + 4) >> 3);
            if (k < 1) k = 1;
            if (k > 20) k = 20;
            hh = 96 + 8 * (u16)k;
            st = 4 * k;
        }
        k0 = (u8)((spr_t0[shape] * hh + 63) >> 6);
        k0 = k0 > st ? k0 - st : 0;
        if (k0 >= rows)
            return FALSE;
        g->bot = g->top + rows;
        g->sc += k0;
        g->top += k0;
    }
    g->step = udiv32_16(16384, h);
    g->xl = xl;
    g->i0 = i0;
    g->i1 = i1;
    /* the tile rows its drawn texels reach (the walls in those tiles take the sprite's colours, so the fewer the
     * better): the scaler gives screen row k (from the top) the texture row k * 64 / h, so the rows of the texture
     * rows t0..t1 are k = ceil(t0 * h / 64) .. ceil((t1 + 1) * h / 64) - 1 (a row less or more would write outside
     * the tile buffer of a hardware sprite) */
    y0 = 48 - (s16)(h >> 1) + (s16)(((u16)spr_t0[shape] * h) >> 6);
    y1 = 48 - (s16)(h >> 1) + (s16)((((u16)spr_t1[shape] + 1) * h + 63) >> 6) - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > VIEW_H - 1) y1 = VIEW_H - 1;
    if (y0 > y1)                            /* (all of it below or above the view: no row) */
    {
        g->r0 = 1;
        g->r1 = 0;
    }
    else
    {
        g->r0 = (u8)y0 >> 3;
        g->r1 = (u8)y1 >> 3;
    }
    return TRUE;
}

/* the texture column that ray i shows of the sprite, 0xFFFF when it shows nothing there */
static u16 strip_col(const VisSprite *vs, u8 i, const SprGeo *g)
{
    s16 ofs;
    u16 col, shape;
    if (vs->depth >= ray_t[i])              /* a wall in front */
        return 0xFFFF;
    ofs = (s16)(i * 4 + 2) - g->xl;
    if (ofs < 0)
        return 0xFFFF;
    col = mulsat8((u16)ofs, g->step);
    shape = vs->shape & VS_SHAPE;
    if (col < spr_x0[shape] || col > spr_x1[shape])
        return 0xFFFF;
    return col;
}

/* ------------------------------------------------------------ the view buffer */
void spr_draw_sw(u8 bankbit) __banked
{
    u8 s, i, ty;
    for (s = 0; s < numvis; s++)
    {
        const VisSprite *vs = &vissprites[s];
        u16 shape = vs->shape & VS_SHAPE, base = spr_off[shape];
        u8 x0 = spr_x0[shape], pal = (6 + spr_slot[s]) | bankbit;
        SprGeo g;

        if ((vs->shape & VS_HW) || !spr_geom(vs, &g))
            continue;
        for (i = (u8)g.i0; i <= (u8)g.i1; i++)
        {
            u16 col = strip_col(vs, i, &g);
            u8 tx = i >> 1;
            if (col == 0xFFFF)
                continue;
            dc_s = base + COLOFS(col - x0);
            dc_bank = spr_bank[shape];
            dc_sc = (u16)g.sc;
            dc_fb = FB + (u16)tx * COLBYTES + ((u16)g.top << 1);
            if (i & 1) spr_right(); else spr_left();
            /* the rows the column has drawn (draw_walls_a clears them the next time this buffer is used) */
            if (g.top < FB_SPAN_TOP[tx])
                FB_SPAN_TOP[tx] = g.top;
            if (g.bot > FB_SPAN_BOT[tx])
                FB_SPAN_BOT[tx] = g.bot;
            for (ty = g.r0; ty <= g.r1; ty++)
                view_attr[(u16)ty * 32 + tx] = pal;
        }
    }
}

/* ------------------------------------------------------------ the hardware sprites */
static const u8 bitof[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

/* Which enemies are hardware sprites: the nearest first, while the OAM entries, the tile pool and the 10 sprites
 * of a line (the weapon's included) allow. */
void spr_plan(void) __banked
{
    u8 load[VIEW_TH];                       /* entries on each tile row of the view */
    u8 cov[(NRAYS + 7) / 8];                /* the rays where a nearer sprite of the view buffer has pixels */
    u8 s, i, smin = 0xFF, oamfree, poolfree = HW_PAIRS;

    hw_n0 = weapon_oam();
    hw_n = 0;
    for (s = 0; s < numvis; s++)
        vissprites[s].shape &= ~VS_HW;
    for (s = numvis; s-- > 0; )             /* the farthest candidate: nothing farther matters */
        if ((vissprites[s].shape & VS_ACTOR) && vissprites[s].size <= HW_MAXH)
            smin = s;
    if (smin == 0xFF)
        return;

    memset(load, 0, sizeof load);
    memset(cov, 0, sizeof cov);
    oamfree = OAM_N - hw_n0;
    for (i = 0; i < hw_n0; i++)             /* the weapon: an entry is 16 lines high */
    {
        u8 b = (u8)(oam_buf[i].y - 16) >> 3;
        if (b < VIEW_TH) load[b]++;
        if (b + 1 < VIEW_TH) load[b + 1]++;
    }

    for (s = numvis; s-- > smin; )
    {
        VisSprite *vs = &vissprites[s];
        SprGeo g;
        u8 nvc = 0, lasttx = 0xFF, P, p, b, ok = 0, blocked = 0;

        if (!spr_geom(vs, &g))
            continue;
        if ((vs->shape & VS_ACTOR) && vs->size <= HW_MAXH && g.r0 <= g.r1)
        {
            P = (g.r1 - g.r0 + 2) >> 1;
            for (i = (u8)g.i0; i <= (u8)g.i1; i++)
            {
                if (strip_col(vs, i, &g) == 0xFFFF)
                    continue;
                if (cov[i >> 3] & bitof[i & 7])
                    blocked = 1;            /* a nearer sprite of the view buffer is on it */
                if ((i >> 1) != lasttx)
                {
                    lasttx = i >> 1;
                    nvc++;
                }
            }
            if (nvc && !blocked && (u8)(nvc * P) <= oamfree && (u8)(nvc * P) <= poolfree)
            {
                ok = 1;
                for (p = 0; p < P; p++)
                {
                    b = g.r0 + 2 * p;
                    if (load[b] + nvc > LINE_MAX || (b + 1 < VIEW_TH && load[b + 1] + nvc > LINE_MAX))
                        ok = 0;
                }
            }
            if (ok)
            {
                for (p = 0; p < P; p++)
                {
                    b = g.r0 + 2 * p;
                    load[b] += nvc;
                    if (b + 1 < VIEW_TH) load[b + 1] += nvc;
                }
                oamfree -= nvc * P;
                poolfree -= nvc * P;
                vs->shape |= VS_HW;
                continue;
            }
            if (!nvc)
                continue;                   /* nothing of it shows */
        }
        /* it goes to the view buffer: the farther sprites must not be hardware sprites where it is */
        for (i = (u8)g.i0; i <= (u8)g.i1; i++)
            if (strip_col(vs, i, &g) != 0xFFFF)
                cov[i >> 3] |= bitof[i & 7];
    }
}


/* Scale the hardware sprites into the staging buffer (the pool of the view buffer being drawn) and make their OAM
 * entries; the tiles of the sprite are a block of tile columns, each column its tile pairs top to bottom, so a
 * column is one run of VRAM tiles. */
void spr_draw_hw(u8 back) __banked
{
    u8 s, n = hw_n0, svbk = rSVBK, tilebase = back ? 64 : 0;
    u16 stage = HW_STAGE(back), st = stage;

    rSVBK = HW_BANK;
    for (s = numvis; s-- > 0; )             /* the nearest first: the hardware gives the lowest entry the front */
    {
        const VisSprite *vs = &vissprites[s];
        u16 shape = vs->shape & VS_SHAPE, base = spr_off[shape];
        u8 x0 = spr_x0[shape], bank = spr_bank[shape], attr = (6 + spr_slot[s]) | 0x08;
        u8 i, c = 0xFF, p, P, curtx = 0xFF, ctx[VIEW_TW];
        u16 colbase = st, pair0 = (st - stage) >> 5;
        SprGeo g;

        if (!(vs->shape & VS_HW) || !spr_geom(vs, &g))
            continue;
        P = (g.r1 - g.r0 + 2) >> 1;
        for (i = (u8)g.i0; i <= (u8)g.i1; i++)
        {
            u16 col = strip_col(vs, i, &g);
            u8 tx = i >> 1;
            if (col == 0xFFFF)
                continue;
            if (tx != curtx)                /* a new tile column: its own, cleared, run of tiles */
            {
                curtx = tx;
                c++;
                ctx[c] = tx;
                colbase = st + (u16)c * P * 32;
                clr32(colbase, P);
            }
            dc_s = base + COLOFS(col - x0);
            dc_bank = bank;
            dc_sc = (u16)g.sc;
            /* row y of the sprite is at 2 * (y - 8 * r0) bytes of the column (the rows above the first texel row
             * are blank, nothing is written there) */
            dc_fb = colbase + (u16)((s16)2 * ((s16)g.top - (s16)(g.r0 << 3)));
            if (i & 1) spr_right(); else spr_left();
        }
        if (c == 0xFF)
            continue;
        for (i = 0; i <= c; i++)
            for (p = 0; p < P; p++)
            {
                u16 pr = pair0 + (u16)i * P + p;
                if (n >= OAM_N || pair_empty(stage + pr * 32))
                    continue;
                oam_buf[n].y = (g.r0 + 2 * p) * 8 + 16;
                oam_buf[n].x = ctx[i] * 8 + 8;
                oam_buf[n].tile = tilebase + 2 * (u8)pr;
                oam_buf[n].attr = attr;
                n++;
            }
        st += (u16)(c + 1) * P * 32;
    }
    rSVBK = svbk;
    hw_bytes = st - stage;
    hw_n = n - hw_n0;
    for (; n < OAM_N; n++)
        oam_buf[n].y = 0;
}

/* the copies that go with the frame (queued before the OAM copy and the flip) */
void spr_queue(u8 back) __banked
{
    if (hw_bytes)
        dma_queue((const void *)HW_STAGE(back), 0xFF, HW_BANK, 1, 0x8000 + (u16)(back ? 64 : 0) * 16, hw_bytes);
    weapon_flush();
}
