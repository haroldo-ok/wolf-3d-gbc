/* hw.c - bank 0: interrupts, palettes, VRAM, DMA queue, banking helpers */
#include "hw.h"
#include "cartram.h"
#include "snd.h"
#include <string.h>

volatile u8 cur_ram_bank, cur_wram_bank = 1, vbl_count;
volatile u8 frame_count;
volatile u32 time_count;
static u16 tc_acc;
u8 pad_new;
u16 pad_f_up, pad_f_down, pad_f_left, pad_f_right;

static u8 oam_dirty;

u8 vmode;
u16 pal_bg[8][4], pal_obj[8][4];
u16 pal_ceil, pal_floor;
u16 pal_hud[8][4];
u8 pal_fade;
u16 pal_fade_to;
u8 pal_tint_r, pal_tint_g, pal_tint_b, pal_tint_lv;
u16 live_bg[8][4], live_obj[8][4], live_floor, live_hud[32];
u8 stat_phase;
volatile u8 pal_pending;        /* 1: BG, 2: OBJ */
u8 cur_lcdc;
u8 map_front;

extern void pal_up_bg(void), pal_up_obj(void), oam_dma_install(void);

/* ------------------------------------------------------------ banking */
void ram_bank(u8 b) { cur_ram_bank = b; mRAMB = b; }
void wram_bank(u8 b) { cur_wram_bank = b; rSVBK = b; }

void rom_read(u8 bank, u16 addr, void *dst, u16 len)
{
    u8 old = cur_rom_bank;
    cur_rom_bank = bank;
    mROMB0 = bank;
    memcpy(dst, (const u8 *)addr, len);
    cur_rom_bank = old;
    mROMB0 = old;
}

u8 tm_get(u8 x, u8 y)
{
    u8 o = rSVBK, v;
    rSVBK = WB_TILEMAP;
    v = ((u8 *)CR_TILEMAP)[((u16)x << 6) | y];
    rSVBK = o;
    return v;
}

void tm_set(u8 x, u8 y, u8 v)
{
    u8 o = rSVBK;
    rSVBK = WB_TILEMAP;
    ((u8 *)CR_TILEMAP)[((u16)x << 6) | y] = v;
    rSVBK = o;
}

/* ------------------------------------------------------------ pad */
/* the pad is sampled at every frame interrupt: how long each direction was held since the last game frame (the
 * game runs at a few frames a second: a tap must not turn a whole frame, nor be missed) */
u8 pad_acc[4], pad_nacc, pad_or;

static u8 pad_raw(void);

u8 pad_read(void)
{
    u8 v;
    di();                       /* the frame interrupt reads the pad too */
    v = pad_raw();
    ei();
    return v;
}

static void pad_sample(void)
{
    u8 p = pad_raw();
    pad_or |= p;
    if (pad_nacc == 255) return;
    pad_nacc++;
    if (p & PAD_UP) pad_acc[0]++;
    if (p & PAD_DOWN) pad_acc[1]++;
    if (p & PAD_LEFT) pad_acc[2]++;
    if (p & PAD_RIGHT) pad_acc[3]++;
}

static u8 pad_raw(void)
{
    u8 a, b;
    rP1 = 0x20;                 /* d-pad */
    a = rP1; a = rP1; a = rP1; a = rP1; a = rP1; a = rP1;
    a = ~a & 0x0F;
    rP1 = 0x10;                 /* buttons (read longer: the lines settle slowly, and this is double speed) */
    b = rP1; b = rP1; b = rP1; b = rP1; b = rP1; b = rP1; b = rP1; b = rP1;
    b = ~b & 0x0F;
    rP1 = 0x30;
    return (u8)((b << 4) | a);
}

/* ------------------------------------------------------------ palettes */
u16 rgb555(u8 r, u8 g, u8 b)
{
    return (u16)((r >> 3) | ((u16)(g >> 3) << 5) | ((u16)(b >> 3) << 10));
}

void lcd_wait_commit(void)
{
    while (pal_pending)
        wait_vblank();
}

/* ------------------------------------------------------------ DMA queue */
extern u16 gd_src, gd_dst;
extern u8 gd_left;
void gdma_run(void);                    /* hw_a.s */
void oam_dma(void);                     /* hw_a.s */
typedef struct { const u8 *src; u16 dst; u16 blocks; u8 srom, swram, vbank, flip; } DmaJob;
#define NJOBS 16
static DmaJob jobs[NJOBS];
static volatile u8 jhead, jtail;
volatile u8 dma_busy;

void dma_queue(const void *src, u8 srom, u8 swram, u8 vbank, u16 dst, u16 len)
{
    DmaJob *j;
    while ((u8)(jtail + 1) % NJOBS == jhead)
        wait_vblank();
    j = &jobs[jtail];
    j->src = src;
    j->dst = dst;
    j->blocks = (len + 15) >> 4;
    j->srom = srom;
    j->swram = swram;
    j->vbank = vbank;
    j->flip = 0;
    di();
    jtail = (u8)(jtail + 1) % NJOBS;
    dma_busy = 1;
    ei();
}

void oam_queue(void)
{
    DmaJob *j;
    while ((u8)(jtail + 1) % NJOBS == jhead)
        wait_vblank();
    j = &jobs[jtail];
    j->blocks = 0;
    j->flip = 2;
    di();
    jtail = (u8)(jtail + 1) % NJOBS;
    dma_busy = 1;
    ei();
}

void flip_queue(void)
{
    DmaJob *j;
    while ((u8)(jtail + 1) % NJOBS == jhead)
        wait_vblank();
    j = &jobs[jtail];
    j->blocks = 0;
    j->flip = 1;
    di();
    jtail = (u8)(jtail + 1) % NJOBS;
    dma_busy = 1;
    ei();
}

void dma_wait(void)
{
    while (dma_busy)
        wait_vblank();
}

/* The jobs run in VBlank only, after the sprites and palettes, in chunks of CHUNK blocks started while LY <= 150.
 * A block of 16 bytes takes 32 dots on hardware (a chunk ~0.85 line); emulators differ (mGBA is slower), so the
 * chunks are small enough to end inside VBlank even at twice that.
 * The budget follows the clock, not a count, so a slower DMA or a late interrupt cannot spill into the visible
 * lines.  Between jobs the timer interrupt (the sampled voice) may run. */

static void run_jobs(void)
{
    u8 oldrom = cur_rom_bank;
    while (jhead != jtail)
    {
        DmaJob *j = &jobs[jhead];
        if (j->flip == 2)                       /* the sprites, right after the tiles queued before them */
        {
            if (rLY > 151)
                break;
            oam_dma();
            j->flip = 0;
            jhead = (u8)(jhead + 1) % NJOBS;
            continue;
        }
        if (j->flip)
        {
            map_front ^= 1;
            cur_lcdc ^= LCDC_BG9C00;
            rLCDC = cur_lcdc;
            j->flip = 0;
            jhead = (u8)(jhead + 1) % NJOBS;
            continue;
        }
        if (!j->blocks)                         /* (an empty job) */
        {
            jhead = (u8)(jhead + 1) % NJOBS;
            continue;
        }
        if (j->srom != 0xFF) { cur_rom_bank = j->srom; mROMB0 = j->srom; }
        if (j->swram) rSVBK = j->swram;
        rVBK = j->vbank;
        gd_src = (u16)j->src;
        gd_dst = j->dst;
        gd_left = (u8)j->blocks;
        gdma_run();
        j->src = (const u8 *)gd_src;
        j->dst = gd_dst;
        j->blocks = gd_left;
        if (gd_left)
            break;                              /* out of VBlank: the rest next time */
        jhead = (u8)(jhead + 1) % NJOBS;
        ei();                                   /* a pending timer interrupt (sampled voice) runs here */
        nop();
        di();
    }
    cur_rom_bank = oldrom;
    mROMB0 = oldrom;
    if (jhead == jtail)
        dma_busy = 0;
}

u8 snd_ticks;

/* ------------------------------------------------------------ interrupts */
void vbl_isr(void)
{
    u8 sv = rSVBK, vb = rVBK;
    if (cur_lcdc & LCDC_ON) rLCDC = cur_lcdc;
    /* first the sprites and the palettes (palette RAM cannot be written while a line is drawn: late writes were
     * lost and left wrong colours for a whole frame), then the copies get the rest of VBlank */
    if (oam_dirty)
    {
        oam_dma();
        oam_dirty = 0;
    }
    if (vmode == VMODE_VIEW) pal_pending |= 1;     /* the line interrupts changed the palettes */
    if (pal_pending & 1) pal_up_bg();
    if (pal_pending & 2) pal_up_obj();
    pal_pending = 0;
    run_jobs();
    pad_sample();
    vbl_count++;
    frame_count++;
    tc_acc += 300;
    time_count += tc_acc >> 8;
    snd_ticks += tc_acc >> 8;
    tc_acc &= 255;
    snd_frame();
    if (vmode == VMODE_VIEW && (cur_lcdc & LCDC_ON))
    {
        rLYC = 46;
        stat_phase = 0;
        rSTAT = STAT_LYC_INT;
    }
    rVBK = vb;
    rSVBK = sv;
}


void oam_set_dirty(void) { oam_dirty = 1; }

void oam_clear(void)
{
    u8 i;
    for (i = 0; i < 40; i++)
        oam_buf[i].y = 0;
    oam_dirty = 1;
}

void oam_flush(void) { oam_dirty = 1; }

void wait_vblank(void)
{
    u8 c = vbl_count;
    if (!(rLCDC & LCDC_ON))
    {
        u16 i;
        for (i = 0; i < 2000; i++) nop();
        vbl_count++;
        return;
    }
    while (c == vbl_count)
        halt();
}

void wait_frames(u16 n)
{
    while (n--)
        wait_vblank();
}

/* ------------------------------------------------------------ LCD */
void lcd_off(void)
{
    if (rLCDC & LCDC_ON)
    {
        while (rLY < 144) ;
        rLCDC = 0;
    }
    cur_lcdc &= ~LCDC_ON;
    rSTAT = 0;
}

void lcd_on(u8 lcdc)
{
    cur_lcdc = lcdc | LCDC_ON;
    map_front = (lcdc & LCDC_BG9C00) ? 1 : 0;
    rLCDC = cur_lcdc;
}

void vram_set(u8 vbank, u16 dst, const u8 *src, u16 len)
{
    u8 ov = rVBK;
    rVBK = vbank;
    memcpy((u8 *)dst, src, len);
    rVBK = ov;
}

void vram_fill8(u8 vbank, u16 dst, u8 v, u16 len)
{
    u8 ov = rVBK;
    rVBK = vbank;
    memset((u8 *)dst, v, len);
    rVBK = ov;
}

void rom_to_vram(u8 rombank, const u8 *src, u8 vbank, u16 dst, u16 len)
{
    u8 oldr = cur_rom_bank, ov = rVBK;
    cur_rom_bank = rombank;
    mROMB0 = rombank;
    rVBK = vbank;
    memcpy((u8 *)dst, src, len);
    rVBK = ov;
    cur_rom_bank = oldr;
    mROMB0 = oldr;
}

/* ------------------------------------------------------------ init: hw_init is in vid.c (banked) */
