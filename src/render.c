/* render.c - the 3-D view.  Rays are cast through the tile map (asm DDA in render_a.s) and the walls and the
 * sprites are composed into a tile frame buffer (WRAM bank 3) that is copied to VRAM by the VBlank DMA queue.
 * Two buffers (VRAM bank 0 / 1) and two maps (9800 / 9C00) are shown alternately. */
#include "game/def.h"
#include <string.h>

/* asm interface (render_a.s) */
extern u8  dd_var, dd_side, dd_val, dc_hi, dc_bank;
extern u16 dd_sx, dd_sy, dd_tx, dd_ty, dd_ptr, dd_t, dc_fb, dc_sc, dc_s;
void dd_run(void);
void fb_clear(void);
void draw_left(void);
void draw_right(void);
void spr_left(void);
void spr_right(void);

#define FB          0xD000
#define COLBYTES    (VIEW_TH * 16)          /* 192: one tile column of the frame buffer */

u16 r_viewx, r_viewy;
u8  render_cast_dirty = 1;
u8  cr_pwpos;
u8  sc_valid;
u16 ray_t[NRAYS];
u8 ray_tex[NRAYS], ray_u[NRAYS], ray_h[NRAYS], ray_pal[NRAYS];
VisSprite vissprites[MAXVISIBLE];
u8  numvis;


fixed viewx, viewy, viewsin, viewcos;
s16 viewangle;
s16 centerx = VIEW_W / 2;
const s16 dirangle[9] = { 0, 45, 90, 135, 180, 225, 270, 315, 360 };

s16 sinv, cosv;                  /* Q8 */

/* ------------------------------------------------------------ helpers */
static u8 hgt(u16 t)
{
    if (t < 512) return hgt_a[t >> 1];
    if (t < 2048) return hgt_b[(t - 512) >> 3];
    if (t < 16384) return hgt_c[(t - 2048) >> 6];
    return 2;
}

/* a signed Q16 number as Q8 */
static s16 q16q8(s32 v)
{
    return (s16)(((s16)((s16 *)&v)[1] << 8) | ((u8 *)&v)[1]);
}

u16 umulq8(u16 t, u16 r);                /* mathz_a.s: (t * r) >> 8, low 16 bits */

/* (t * r) >> 8 for t unsigned Q8, r signed Q8 */
static s16 mulq8(u16 t, s16 r)
{
    if (r >= 0)
        return (s16)umulq8(t, (u16)r);
    return -(s16)umulq8(t, (u16)(-r));
}

/* (a * b) >> 8, a <= 256, b unsigned 16 bit (it cannot overflow) */
#define mulsat8(a, b) umulq8((a), (b))

/* ------------------------------------------------------------ casting */
static void cast_rays(void)
{
    s32 ax, ay, stepx, stepy;
    s16 rx, ry, sxh, syh;               /* the ray direction in Q8 and its step: integer part ... */
    u8 fxl, fyl, sxl, syl;              /* ... and the fraction (the Q16 values without q16q8 and 32 bit sums) */
    const u8 *fam;
    u8 i, fx = (u8)r_viewx, fy = (u8)r_viewy, px = r_viewx >> 8, py = r_viewy >> 8;
    u8 *map0 = (u8 *)CR_TILEMAP + ((u16)px << 6) + py;
    u8 pwx = (u8)pwallx, pwy = (u8)pwally;
    u8 pwstate = pwallstate != 0;
    u8 famoff = (u8)gamestate.mapon;
    TM_ON();

    stepx = smul16(sinv, TAN36_STEP) >> 8;
    stepy = smul16(cosv, TAN36_STEP) >> 8;
    ax = ((s32)cosv << 8) - ((stepx * (NRAYS - 1)) >> 1);
    ay = -((s32)sinv << 8) - ((stepy * (NRAYS - 1)) >> 1);

    rx = q16q8(ax); fxl = (u8)ax;
    ry = q16q8(ay); fyl = (u8)ay;
    sxh = q16q8(stepx); sxl = (u8)stepx;
    syh = q16q8(stepy); syl = (u8)stepy;
    fam = lvl_fam + ((u16)famoff << 6);
    for (i = 0; i < NRAYS; i++)
    {
        u16 tdx, tdy, dist, t;
        u8 v, side, tex = 0, u = 0, var = 0;

        if (rx >= 0) { tdx = recip_q8[rx]; dist = 256 - fx; }
        else         { tdx = recip_q8[-rx]; dist = fx; var |= 1; }
        if (ry >= 0) { tdy = recip_q8[ry]; dd_sy = mulsat8(256 - fy, tdy); }
        else         { tdy = recip_q8[-ry]; dd_sy = mulsat8(fy, tdy); var |= 2; }
        dd_sx = mulsat8(dist, tdx);
        dd_tx = tdx;
        dd_ty = tdy;
        dd_var = var;
        dd_ptr = (u16)map0;

        for (;;)
        {
            dd_run();
            v = dd_val;
            side = dd_side;
            t = dd_t;
            if (!(v & 0x80))
            {
                u8 id = v & 0x3F;
                tex = side ? wall_y_tex[id] : wall_x_tex[id];
                if (v & 0x40)
                {
                    /* a wall beside a door: the door frame on the face that looks at the door */
                    u8 *prev = (u8 *)dd_ptr;
                    if (side) prev += (var & 2) ? 1 : -1;
                    else      prev += (var & 1) ? 64 : -64;
                    if ((*prev & 0xC0) == 0x80)
                        tex = side ? DOORW_FRAME : DOORW_FRAME + 1;
                }
                break;
            }
            if ((v & 0xC0) == 0x80)
            {
                /* a door in the middle of its tile */
                u8 n = v & 0x3F, vert = doorvert8[n], frac, pos = doorpos8[n];
                u16 tm;
                if (vert)
                {
                    if (side) goto pass;
                    tm = t + (dd_tx >> 1);
                    if (dd_sy <= tm) goto pass;
                    frac = (u8)((u16)r_viewy + (u16)mulq8(tm, ry));
                    if (frac < pos) goto pass;
                    u = (frac - pos) >> 2;
                    if (var & 1) u = 63 - u;
                    tex = doorpage8[n] + 1;
                }
                else
                {
                    if (!side) goto pass;
                    tm = t + (dd_ty >> 1);
                    if (dd_sx <= tm) goto pass;
                    frac = (u8)((u16)r_viewx + (u16)mulq8(tm, rx));
                    if (frac < pos) goto pass;
                    u = (frac - pos) >> 2;
                    if (!(var & 2)) u = 63 - u;
                    tex = doorpage8[n];
                }
                t = tm;
                goto have_u;
            }
            else
            {
                /* the moving push wall: its face moves with cr_pwpos (1/64 tile) along its direction */
                u8 id = v & 0x3F;
                u8 tx_ = (u8)(((u16)dd_ptr - (u16)CR_TILEMAP) >> 6), ty_ = (u8)(((u16)dd_ptr - (u16)CR_TILEMAP) & 63);
                if (pwstate && tx_ == pwx && ty_ == pwy)
                {
                    u8 axis_x = (pwalldir & 1);                 /* east / west */
                    u8 pos_dir = (pwalldir == di_east || pwalldir == di_south);
                    u8 rayneg = side ? (var & 2) : (var & 1);
                    if (axis_x == !side && pos_dir == !rayneg)
                    {
                        u16 off = (u16)cr_pwpos << 2, tm;
                        tm = t + mulsat8(off, side ? dd_ty : dd_tx);
                        if ((side ? dd_sx : dd_sy) <= tm)
                            goto pass;
                        t = tm;
                    }
                }
                tex = side ? wall_y_tex[id] : wall_x_tex[id];
                break;
            }
pass:
            if (side == 0)
            {
                u16 n = dd_sx + dd_tx;
                dd_sx = n < dd_sx ? 0xFFFF : n;
            }
            else
            {
                u16 n = dd_sy + dd_ty;
                dd_sy = n < dd_sy ? 0xFFFF : n;
            }
        }

        /* the position along the wall -> texture column */
        if (side == 0)
        {
            u = (u8)((u16)r_viewy + (u16)mulq8(t, ry)) >> 2;
            if (var & 1) u = 63 - u;
        }
        else
        {
            u = (u8)((u16)r_viewx + (u16)mulq8(t, rx)) >> 2;
            if (!(var & 2)) u = 63 - u;
        }
have_u:
        if (tex == 0xFF) tex = 0;
        ray_t[i] = t;
        ray_tex[i] = tex;
        ray_u[i] = u;
        ray_h[i] = hgt(t);
        ray_pal[i] = fam[tex];
        {
            u16 c = (u16)fxl + sxl;
            fxl = (u8)c;
            rx += sxh + (s16)(c >> 8);
            c = (u16)fyl + syl;
            fyl = (u8)c;
            ry += syh + (s16)(c >> 8);
        }
    }
    TM_OFF();
}

/* ------------------------------------------------------------ drawing */
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

static void draw_walls(void)
{
    u8 i;
    for (i = 0; i < NRAYS; i++)
    {
        const u8 *sc;
        u8 top, tex = ray_tex[i];
        scaler_for(ray_h[i], &sc, &top);
        dc_sc = (u16)sc;
        dc_fb = FB + (u16)(i >> 1) * COLBYTES + ((u16)top << 1);
        dc_hi = 0x40 + ray_u[i];
        dc_bank = tex_bank[tex];
        if (i & 1) draw_right(); else draw_left();
    }
}

/* the palette slots of the sprites (BG palettes 6 and 7) */
static u8 slot_pool[2] = { 0xFF, 0xFF };
static u8 spr_slot[MAXVISIBLE];

static void assign_slots(void)
{
    u8 i, n = 0, np[2] = { 0xFF, 0xFF }, k, changed = 0;
    for (i = numvis; i-- > 0; )             /* the nearest sprite first */
    {
        u8 p = spr_pool[vissprites[i].shape];
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
                pal_bg[6 + k][1 + j] = pool_pal[np[k] * 3 + j];
            changed = 1;
        }
    if (changed)
        pal_commit();
}

static void draw_sprites(u8 bankbit)
{
    u8 s, i;
    for (s = 0; s < numvis; s++)
    {
        VisSprite *vs = &vissprites[s];
        u16 shape = vs->shape;
        u8 h = vs->size, top, r0, r1, pal = (6 + spr_slot[s]) | bankbit;
        s16 xl = vs->cx - (h >> 1);
        s16 i0 = xl >> 2, i1 = (xl + h - 1) >> 2;
        u16 step, base = spr_off[shape];
        u8 x0 = spr_x0[shape], x1 = spr_x1[shape];
        const u8 *sc;

        if (i0 < 0) i0 = 0;
        if (i1 > NRAYS - 1) i1 = NRAYS - 1;
        if (i1 < i0)                            /* off the view: the u8 loop below would never end (i1 = 255) */
            continue;
        scaler_for(h, &sc, &top);
        step = udiv32_16(16384, h);
        {
            /* the tile rows that get the sprite's palette: only those its drawn texels reach (the walls in those
             * tiles take the sprite's colours, so the fewer the better) */
            s16 y0 = 48 - (s16)(h >> 1) + (s16)(((u16)spr_t0[shape] * h) >> 6);
            s16 y1 = 48 - (s16)(h >> 1) + (s16)((((u16)spr_t1[shape] + 1) * h) >> 6) - 1;
            if (y0 < 0) y0 = 0;
            if (y1 > VIEW_H - 1) y1 = VIEW_H - 1;
            if (y0 > y1)                        /* (all of it below or above the view: no row) */
            {
                r0 = 1;
                r1 = 0;
            }
            else
            {
                r0 = (u8)y0 >> 3;
                r1 = (u8)y1 >> 3;
            }
        }
        for (i = (u8)i0; i <= (u8)i1; i++)
        {
            s16 ofs;
            u16 col;
            u8 ty, tx = i >> 1;
            if (vs->depth >= ray_t[i])
                continue;
            ofs = (s16)(i * 4 + 2) - xl;
            if (ofs < 0) continue;
            col = mulsat8((u16)ofs, step);
            if (col < x0 || col > x1)
                continue;
            dc_s = base + ((col - x0) << 6);
            dc_bank = spr_bank[shape];
            dc_sc = (u16)sc;
            dc_fb = FB + (u16)tx * COLBYTES + ((u16)top << 1);
            if (i & 1) spr_right(); else spr_left();
            for (ty = r0; ty <= r1; ty++)
                view_attr[(u16)ty * 32 + tx] = pal;
        }
    }
}

/* ------------------------------------------------------------ sprites of the world */
static s16 cqx, cqy;                    /* the camera in Q8 tiles */

/* world position (Q8 tiles) -> screen centre, height and depth; FALSE when it can't be seen */
static u8 project(s16 dx, s16 dy, s16 *pcx, u8 *psize, u16 *pdepth)
{
    s32 d32 = smul16(dx, cosv) - smul16(dy, sinv);
    s32 l32;
    s16 depth, lat, cx;
    u8 h;
    depth = q16q8(d32);
    if (depth < 64)
        return FALSE;
    l32 = smul16(dx, sinv) + smul16(dy, cosv);
    lat = q16q8(l32);
    if (lat > depth + 160 || lat < -depth - 160)
        return FALSE;
    h = hgt((u16)depth);
    {
        s32 n = smul16(lat, FOCAL);
        if (n >= 0) cx = VIEW_W / 2 + (s16)udiv32_16(n, (u16)depth);
        else        cx = VIEW_W / 2 - (s16)udiv32_16(-n, (u16)depth);
    }
    if (cx + (h >> 1) < 0 || cx - (h >> 1) >= VIEW_W)
        return FALSE;
    *pcx = cx;
    *psize = h;
    *pdepth = (u16)depth;
    return TRUE;
}

/* is any ray in front of the sprite's wall distance? */
static u8 sprite_seen(s16 cx, u8 h, u16 depth)
{
    s16 i0 = (cx - (h >> 1)) >> 2, i1 = (cx + (h >> 1)) >> 2;
    if (i0 < 0) i0 = 0;
    if (i1 > NRAYS - 1) i1 = NRAYS - 1;
    for (; i0 <= i1; i0++)
        if (ray_t[i0] > depth)
            return TRUE;
    return FALSE;
}

static void add_sprite(s16 cx, u8 h, u16 depth, u16 shape)
{
    VisSprite *vs;
    u8 i;
    if (numvis >= MAXVISIBLE)
        return;
    /* far to near: insertion sort */
    for (i = numvis; i > 0 && vissprites[i - 1].depth < depth; i--)
        vissprites[i] = vissprites[i - 1];
    vs = &vissprites[i];
    vs->cx = cx;
    vs->size = h;
    vs->depth = depth;
    vs->shape = shape;
    numvis++;
}

s16 CalcRotate (objtype *ob) B
{
    s16 angle, va;

    va = player->angle + (centerx - ob->viewx) / 8;
    angle = (va - 180) - dirangle[ob->dir];
    angle += ANGLES / 16;
    while (angle >= ANGLES) angle -= ANGLES;
    while (angle < 0) angle += ANGLES;
    if (ob->state->rotate == 2)             /* 2 rotation pain frame */
        return 4 * (angle / (ANGLES / 2));
    return angle / (ANGLES / 8);
}

/* the statics near the camera */
#define NEARMAX 80
static u16 near_idx[NEARMAX];
static u8 near_n;
static u8 near_tx, near_ty;

static void build_near(u8 tx, u8 ty)
{
    statobj_t *st;
    u8 n = 0;
    near_tx = tx;
    near_ty = ty;
    sc_valid = 1;
    for (st = statobjlist; st != laststatobj; st++)
    {
        s8 dx, dy;
        if (st->type == STAT_REMOVED)
            continue;
        dx = (s8)(st->tilex - tx);
        dy = (s8)(st->tiley - ty);
        if (dx > 13 || dx < -13 || dy > 13 || dy < -13)
            continue;
        if (n < NEARMAX)
            near_idx[n++] = (u16)(st - statobjlist);
    }
    near_n = n;
}

static void draw_scaleds(void)
{
    objtype *obj;
    u8 i, tx = (u8)(r_viewx >> 8), ty = (u8)(r_viewy >> 8);
    s16 cx;
    u8 h;
    u16 depth;

    numvis = 0;
    if (!sc_valid || tx != near_tx || ty != near_ty)
        build_near(tx, ty);
    for (i = 0; i < near_n; i++)
    {
        statobj_t *st = &statobjlist[near_idx[i]];
        s16 dx, dy;
        if (st->type == STAT_REMOVED)
            continue;
        dx = (s16)(((u16)st->tilex << 8) + 128) - (s16)r_viewx;
        dy = (s16)(((u16)st->tiley << 8) + 128) - (s16)r_viewy;
        if (!project(dx, dy, &cx, &h, &depth))
            continue;
        if (!sprite_seen(cx, h, depth))
            continue;
        add_sprite(cx, h, depth, STAT_SHAPE(st->type));
    }

    for (obj = player->next; obj; obj = obj->next)
    {
        s16 shape = obj->state->shapenum, dx, dy;
        if (!(obj->flags & (FL_VISABLE | FL_ATTACKMODE)) && !obj->active && !areabyplayer[obj->areanumber])
        {
            obj->flags &= ~FL_VISABLE;
            continue;
        }
        /* bits 8..23 of the position (Q8 tiles), read as bytes: SDCC 4.2 miscompiled the s32 >> 8 here (dy took
         * stale bytes of dx), which put every actor off screen */
        dx = (s16)(*(const u16 *)((const u8 *)&obj->x + 1) - r_viewx);
        dy = (s16)(*(const u16 *)((const u8 *)&obj->y + 1) - r_viewy);
        if (dx > 4000 || dx < -4000 || dy > 4000 || dy < -4000 ||
            !project(dx, dy, &cx, &h, &depth))
        {
            obj->viewheight = 0;
            obj->flags &= ~FL_VISABLE;
            continue;
        }
        obj->viewx = cx;
        obj->viewheight = (u16)h << 1;
        obj->transx = (s32)depth << 8;
        if (!sprite_seen(cx, h, depth))
        {
            obj->flags &= ~FL_VISABLE;
            continue;
        }
        if (!obj->active)
            obj->active = ac_yes;
        if (shape == -1)
            shape = obj->temp1;
        if (obj->state->rotate)
            shape += CalcRotate(obj);
        obj->flags |= FL_VISABLE;
        add_sprite(cx, h, depth, (u16)shape);
    }
}

/* ------------------------------------------------------------ the frame */
void render_present(void)
{
    u8 back = map_front ^ 1;
    u16 map = back ? 0x9C00 : 0x9800;
    dma_queue((const void *)FB, 0xFF, WB_FRAME, back, 0x8000, VIEW_TILES * 16);
    dma_queue(view_attr, 0xFF, 0, 1, map, VIEW_TH * 32);
    flip_queue();
}

u16 refresh_count;                      /* frames drawn (the tests see the game is alive) */

void ThreeDRefresh (void) B
{
    u8 back, bankbit, tx, ty;
    refresh_count++;

    viewangle = player->angle;
    viewsin = fsin(viewangle);
    viewcos = fcos(viewangle);
    viewx = player->x - FixedByFrac(FOCALLENGTH, viewcos);
    viewy = player->y + FixedByFrac(FOCALLENGTH, viewsin);
    r_viewx = (u16)(viewx >> 8);
    r_viewy = (u16)(viewy >> 8);
    {
        u16 ai = (u16)(((u32)(u16)viewangle * 728) >> 8);  /* degrees -> 1/1024 turns (32 bits: 359 * 728 does not fit in 16) */
        sinv = sin1024[ai];
        cosv = sin1024[ai + 256];
    }

    cast_rays();
    draw_scaleds();
    assign_slots();

    dma_wait();
    back = map_front ^ 1;
    bankbit = back ? 0x08 : 0;
    /* palette of each tile column: the nearer of its two rays (the first row computed, the others copied) */
    {
        u8 *q = view_attr;
        const u16 *rt = ray_t;
        const u8 *rp = ray_pal;
        for (tx = 0; tx < VIEW_TW; tx++, rt += 2, rp += 2)
            *q++ = (rt[0] <= rt[1] ? rp[0] : rp[1]) | bankbit;
        for (ty = 1; ty < VIEW_TH; ty++)
            memcpy(view_attr + (u16)ty * 32, view_attr, VIEW_TW);
    }
    rSVBK = WB_FRAME;
    fb_clear();
    draw_walls();
    draw_sprites(bankbit);
    rSVBK = WB_ACTORAT;
    hud_flush();
    weapon_show(gamestate.weapon, gamestate.weaponframe);  /* the attack frames (only the changes cost anything) */
    render_present();
}

/* ------------------------------------------------------------ setup */
void render_init(void) __banked
{
    u8 tx, ty, t, a;
    /* both maps: tile numbers of the view rows (VRAM bank 0), attributes (bank 1) */
    for (ty = 0; ty < VIEW_TH; ty++)
        for (tx = 0; tx < VIEW_TW; tx++)
        {
            t = tx * VIEW_TH + ty;
            vram_set(0, 0x9800 + ty * 32 + tx, &t, 1);
            vram_set(0, 0x9C00 + ty * 32 + tx, &t, 1);
            a = 0;
            vram_set(1, 0x9800 + ty * 32 + tx, &a, 1);
            a = 0x08;
            vram_set(1, 0x9C00 + ty * 32 + tx, &a, 1);
        }
    memset(view_attr, 0, sizeof(view_attr));
    slot_pool[0] = slot_pool[1] = 0xFF;
    sc_valid = 0;
}

void render_level_palettes(u8 level) __banked
{
    u8 f, j;
    for (f = 0; f < NFAM; f++)
        for (j = 0; j < 3; j++)
            pal_bg[f][1 + j] = lvl_pal[(u16)level * (NFAM * 3) + f * 3 + j];
    pal_ceil = lvl_ceil[level];
    pal_floor = lvl_floor[level];
}
