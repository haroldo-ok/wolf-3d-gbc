/* render.c - the 3-D view.  Rays are cast through the tile map (asm DDA in render_a.s) and the walls and most of the
 * sprites are composed into a tile frame buffer (WRAM bank 3) that is copied to VRAM by the VBlank DMA queue; the
 * enemies are hardware sprites when they fit (rspr.c).
 * Two buffers (VRAM bank 0 / 1) and two maps (9800 / 9C00) are shown alternately. */
#include "game/def.h"
#include <string.h>

/* asm interface (render_a.s) */
extern u8  dd_var, dd_side, dd_val, dc_hi, dc_bank, dc_n;
extern u16 dd_sx, dd_sy, dd_tx, dd_ty, dd_ptr, dd_t, dc_fb, dc_sc;
void dd_run(void);
void draw_left(void);
void draw_right(void);

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
/* cast_a.s: the common path of a ray; its frame constants in HRAM */
u8 cr_ray(void);
void draw_walls_a(void);
#define CR_I   (*(volatile u8 *)0xFFC1)
#define CR_FX  (*(volatile u8 *)0xFFC2)
#define CR_FY  (*(volatile u8 *)0xFFC3)
#define CR_MAP (*(volatile u16 *)0xFFC4)
#define CR_FAM (*(volatile u16 *)0xFFC6)
#define CR_RX  (*(volatile s16 *)0xFFCE)
#define CR_RY  (*(volatile s16 *)0xFFD0)
#define CR_FXL (*(volatile u8 *)0xFFD2)
#define CR_FYL (*(volatile u8 *)0xFFD3)
#define CR_SXL (*(volatile u8 *)0xFFD4)
#define CR_SX  (*(volatile s16 *)0xFFD5)
#define CR_SYL (*(volatile u8 *)0xFFD7)
#define CR_SY  (*(volatile s16 *)0xFFD8)

static void cast_rays(void)
{
    s32 ax, ay, stepx, stepy;
    u8 i, px = r_viewx >> 8, py = r_viewy >> 8;
    u8 *map0 = (u8 *)CR_TILEMAP + ((u16)px << 6) + py;
    u8 pwx = (u8)pwallx, pwy = (u8)pwally;
    u8 pwstate = pwallstate != 0;
    TM_ON();

    stepx = smul16(sinv, TAN36_STEP) >> 8;
    stepy = smul16(cosv, TAN36_STEP) >> 8;
    /* (NRAYS - 1) / 2 = 19.5 steps back from the centre: 39 * step = 32 * step + 8 * step - step (no s32 multiply) */
    ax = ((s32)cosv << 8) - (((stepx << 5) + (stepx << 3) - stepx) >> 1);
    ay = -((s32)sinv << 8) - (((stepy << 5) + (stepy << 3) - stepy) >> 1);

    /* the ray direction in Q8 and its step (cr_ray steps it): Q8 part and the fraction below (the Q16 values without
     * 32 bit sums) */
    CR_RX = q16q8(ax); CR_FXL = (u8)ax;
    CR_RY = q16q8(ay); CR_FYL = (u8)ay;
    CR_SX = q16q8(stepx); CR_SXL = (u8)stepx;
    CR_SY = q16q8(stepy); CR_SYL = (u8)stepy;
    CR_FX = (u8)r_viewx;
    CR_FY = (u8)r_viewy;
    CR_MAP = (u16)map0;
    CR_FAM = (u16)(lvl_fam + ((u16)(u8)gamestate.mapon << 6));
    for (i = 0; i < NRAYS; i++)
    {
        CR_I = i;
        if (cr_ray())
        {
            /* the DDA stopped at a door or the push wall: on from there */
            s16 rx = CR_RX, ry = CR_RY;
            u16 t;
            u8 v, side, tex = 0, u = 0, var = dd_var;
            for (;;)
            {
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
                dd_run();
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
            ray_pal[i] = lvl_fam[((u16)(u8)gamestate.mapon << 6) + tex];
        }
    }
    TM_OFF();
}

/* ------------------------------------------------------------ drawing */
/* the walls: draw_walls_a (cast_a.s) */

/* ------------------------------------------------------------ sprites of the world */
static s16 cqx, cqy;                    /* the camera in Q8 tiles */

/* world position (Q8 tiles) -> screen centre, height and depth; FALSE when it can't be seen (project_a, cast_a.s:
 * depth = (dx cos - dy sin) >> 8 >= 64, lat = (dx sin + dy cos) >> 8 within depth + 160, centre 80 + lat * FOCAL / depth) */
u8 project_a(s16 dx, s16 dy);
#define PJ_D   (*(volatile u16 *)0xFFE7)
#define PR_CX  (*(volatile s16 *)0xFFEE)
#define PR_H   (*(volatile u8 *)0xFFF0)

static u8 project(s16 dx, s16 dy, s16 *pcx, u8 *psize, u16 *pdepth)
{
    if (!project_a(dx, dy))
        return FALSE;
    *pcx = PR_CX;
    *psize = PR_H;
    *pdepth = PJ_D;
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

static void add_sprite(s16 cx, u8 h, u16 depth, u16 shape)    /* (shape: | VS_ACTOR for the actors) */
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
        s16 shape, dx, dy;
        /* the actors that are not seen, not active and not in an area connected to the player's (most of them: the
         * test comes first; their FL_VISABLE is already clear) */
        if (!(obj->flags & (FL_VISABLE | FL_ATTACKMODE)) && !obj->active && !areabyplayer[obj->areanumber])
            continue;
        shape = obj->state->shapenum;
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
        add_sprite(cx, h, depth, (u16)shape | VS_ACTOR);
    }
}

/* ------------------------------------------------------------ the frame */
/* Two frames can be in the DMA queue: frame k is drawn into the buffers of parity k & 1 (the view buffer in WRAM bank 3
 * or 7, the enemies' tiles in bank 6 at 0xD000 or 0xD400, a copy of the OAM in bank 6 at 0xDC00 or 0xDD00) while the
 * other frame is still being copied; the attributes (one buffer) are copied first. */
#define FB_BANK(p)   ((p) ? WB_FRAME2 : WB_FRAME)
#define OAM_COPY(p)  ((p) ? 0xDD : 0xDC)
static u8 r_back;                       /* the VRAM bank / BG map of the frame being drawn */

static void render_present(u8 back)
{
    u16 map = back ? 0x9C00 : 0x9800;
    dma_queue(view_attr, 0xFF, 0, 1, map, VIEW_TH * 32);
    attr_mark_queue();                  /* (the next frame may write view_attr once that is done) */
    dma_queue((const void *)FB, 0xFF, FB_BANK(back), back, VIEW_VRAM, VIEW_TILES * 16);
    spr_queue(back);                    /* the enemies' tiles, the weapon's tiles */
    rSVBK = WB_SPRITES;
    memcpy((void *)((u16)OAM_COPY(back) << 8), oam_buf, 160);
    rSVBK = WB_ACTORAT;
    oam_queue(WB_SPRITES, OAM_COPY(back));  /* the sprites and the flip show everything of this frame together */
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
        u16 ai = umulq8((u16)viewangle, 728);   /* degrees -> 1/1024 turns: (a * 728) >> 8 (359 * 728 needs 18 bits) */
        sinv = sin1024[ai];
        cosv = sin1024[ai + 256];
    }

    cast_rays();
    draw_scaleds();
    spr_slots();

    while (attr_busy)                   /* the previous frame's attributes are copied */
        wait_vblank();
    if (!vframes)                       /* nothing in the queue: the frame goes to what is not shown */
        r_back = map_front ^ 1;
    back = r_back;
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
    weapon_show(gamestate.weapon, gamestate.weaponframe);  /* the attack frames (only the changes cost anything) */
    spr_plan();                         /* the weapon's OAM entries, then which enemies are hardware sprites */
    while (vframes >= 2)                /* the frame before the previous one has left this parity's buffers */
        wait_vblank();
    rSVBK = FB_BANK(back);
    draw_walls_a();                     /* (it clears what the walls do not cover) */
    spr_draw_sw(bankbit);
    spr_draw_hw(back);
    rSVBK = WB_ACTORAT;
    hud_flush();
    render_present(back);
    r_back = back ^ 1;
}

/* ------------------------------------------------------------ setup */
void render_init(void) __banked
{
    u8 tx, ty, t, a;
    /* both maps: tile numbers of the view rows (VRAM bank 0), attributes (bank 1) */
    for (ty = 0; ty < VIEW_TH; ty++)
        for (tx = 0; tx < VIEW_TW; tx++)
        {
            t = (u8)(tx * VIEW_TH + ty + 128);   /* the signed tile addressing: index 0 is tile 128 (0x8800) */
            vram_set(0, 0x9800 + ty * 32 + tx, &t, 1);
            vram_set(0, 0x9C00 + ty * 32 + tx, &t, 1);
            a = 0;
            vram_set(1, 0x9800 + ty * 32 + tx, &a, 1);
            a = 0x08;
            vram_set(1, 0x9C00 + ty * 32 + tx, &a, 1);
        }
    memset(view_attr, 0, sizeof(view_attr));
    for (a = 0; a < 2; a++)             /* the frame buffers: every row of every column may hold something */
    {
        rSVBK = a ? WB_FRAME2 : WB_FRAME;
        memset(FB_SPAN_TOP, 0, VIEW_TW);
        memset(FB_SPAN_BOT, VIEW_H, VIEW_TW);
    }
    rSVBK = WB_ACTORAT;
    spr_reset();
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
