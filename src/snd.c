/* snd.c - sound for the Game Boy Color (bank 10): music and effects are event streams (tools/gb_audio.py, the
 * format of an SN76489) played on the APU from the frame interrupt at 70 ticks a second; digitised voices are
 * 3 bit samples written to the master volume by the timer interrupt (snd_a.s).
 *
 *   tone 0 -> wave channel (a square wave in wave RAM)    tone 1 -> pulse channel 1
 *   tone 2 -> pulse channel 2 (the effect's, when one plays)   noise -> noise channel (idem)
 *
 * The streams live in the data banks, so they are read through rom_read (bank 0): this code is itself in a
 * switchable bank and cannot map another one over its own.  */
#include "game/def.h"
#include "gbc.h"
#include "hw.h"
#include "snd.h"
#include "assets.h"

typedef struct
{
    u8 on, bank, wait, pri;
    u16 p;
    u16 per[3];
    u8 vol[4];
    u8 nctl;
} Voice;

extern u8 pcm_on, pcm_ph, pcm_bnk;
extern u16 pcm_ptr, pcm_cnt;

u8 SoundOn = 1, MusicOn = 1, DigiOn = 1;      /* DigiOn: 0 none, 1 voices, 2 all */

static Voice mus, sfx;
static u16 cur_per[3];
static u8 cur_vol[4], cur_nctl;
static u8 w3_on;
static u16 cur_sound;
static u8 cur_song = 0xFF;

/* attenuation (SN76489, 2 dB steps) -> volume 0..15 */
static const u8 vtab[16] = { 15, 12, 9, 8, 6, 5, 4, 3, 2, 2, 2, 1, 1, 1, 1, 0 };

#define WAVERAM ((volatile u8 *)0xFF30)

static void wave_square(void)
{
    u8 i;
    rNR30 = 0;
    for (i = 0; i < 8; i++) WAVERAM[i] = 0xFF;
    for (i = 8; i < 16; i++) WAVERAM[i] = 0x00;
    rNR30 = 0x80;
    w3_on = 0;
}

static void wave_dc(void)
{
    u8 i;
    rNR30 = 0;
    for (i = 0; i < 16; i++) WAVERAM[i] = 0xFF;
    rNR30 = 0x80;
}

static void silence(Voice *v)
{
    v->vol[0] = v->vol[1] = v->vol[2] = v->vol[3] = 15;
}

static void invalidate(void)
{
    u8 i;
    for (i = 0; i < 3; i++) cur_per[i] = 0xFFFF;
    for (i = 0; i < 4; i++) cur_vol[i] = 0xFF;
    cur_nctl = 0xFF;
}

static u8 fetch(Voice *v)
{
    u8 b;
    rom_read(v->bank, v->p, &b, 1);
    v->p++;
    return b;
}

/* one 70 Hz tick of a voice */
static void step(Voice *v)
{
    u8 b, ch;
    if (!v->on) return;
    if (--v->wait) return;
    for (;;)
    {
        u16 at = v->p;
        b = fetch(v);
        if (b == 0)
        {
            u16 off = fetch(v);
            off |= (u16)fetch(v) << 8;
            if (!off)
            {
                v->on = 0;
                silence(v);
                v->p = at;
                return;
            }
            v->p = at - off;
            continue;
        }
        if (!(b & 0x80))
        {
            v->wait = b;
            return;
        }
        ch = (b >> 5) & 3;
        v->vol[ch] = b & 15;
        if (b & 0x10)
        {
            if (ch == 3)
                v->nctl = fetch(v);
            else
            {
                u16 lo = fetch(v);
                v->per[ch] = lo | ((u16)fetch(v) << 8);
            }
        }
    }
}

/* SN76489 period -> APU frequency register: x = 2048 - p * 1200 / 1024 (pulse), the wave channel half of that */
#include "sndtab.h"
static u16 pulse_x(u16 p, u8 wave)
{
    u16 d;
    p &= 1023;
    d = pdelta[p];
    if (wave)
        d = (d >> 1) ? (d >> 1) : 1;
    return 2048 - d;
}

static void mix(void)
{
    Voice *v;
    u8 vol;
    u16 per, x;
    /* channel 0 -> wave */
    vol = vtab[mus.vol[0]];
    per = mus.per[0];
    if (per != cur_per[0] || vol != cur_vol[0])
    {
        u8 code = vol >= 12 ? 1 : vol >= 6 ? 2 : vol >= 2 ? 3 : 0;
        x = pulse_x(per, 1);
        rNR33 = (u8)x;
        rNR32 = code << 5;
        if (code && !w3_on)
            rNR34 = 0x80 | (x >> 8);
        else
            rNR34 = x >> 8;
        w3_on = code != 0;
        cur_per[0] = per;
        cur_vol[0] = vol;
    }
    /* channel 1 -> pulse 1 */
    vol = vtab[mus.vol[1]];
    per = mus.per[1];
    if (per != cur_per[1] || vol != cur_vol[1])
    {
        x = pulse_x(per, 0);
        rNR11 = 0x80;
        rNR13 = (u8)x;
        if (vol != cur_vol[1])
        {
            rNR12 = vol << 4;
            rNR14 = 0x80 | (x >> 8);
        }
        else
            rNR14 = x >> 8;
        cur_per[1] = per;
        cur_vol[1] = vol;
    }
    v = sfx.on ? &sfx : &mus;
    /* channel 2 -> pulse 2 */
    vol = vtab[v->vol[2]];
    per = v->per[2];
    if (per != cur_per[2] || vol != cur_vol[2])
    {
        x = pulse_x(per, 0);
        rNR21 = 0x80;
        rNR23 = (u8)x;
        if (vol != cur_vol[2])
        {
            rNR22 = vol << 4;
            rNR24 = 0x80 | (x >> 8);
        }
        else
            rNR24 = x >> 8;
        cur_per[2] = per;
        cur_vol[2] = vol;
    }
    /* noise */
    vol = vtab[v->vol[3]];
    if (v->nctl != cur_nctl || vol != cur_vol[3])
    {
        u8 n = v->nctl & 3;
        u8 f = (n == 0 ? 0x40 : n == 1 ? 0x50 : 0x60) | ((v->nctl & 4) ? 0 : 8);
        rNR43 = f;
        if (vol != cur_vol[3])
        {
            rNR42 = vol << 4;
            rNR44 = 0x80;
        }
        cur_nctl = v->nctl;
        cur_vol[3] = vol;
    }
}

/* the chip as the streams left it: after a sampled voice */
static void pcm_stop(void)
{
    rTAC = 0;
    rIE &= ~IE_TIMER;
    pcm_on = 0;
    rNR50 = 0x77;
    rNR51 = 0xFF;
    wave_square();
    invalidate();
}

void snd_frame(void) __banked
{
    u8 k;
    if (pcm_on == 2)
        pcm_stop();
    for (k = snd_ticks; k; k--)
    {
        step(&mus);
        step(&sfx);
    }
    snd_ticks = 0;
    if (!pcm_on)
        mix();
}

void snd_init(void) __banked
{
    u8 i;
    di();
    rNR52 = 0x80;
    rNR50 = 0x77;
    rNR51 = 0xFF;
    mus.on = sfx.on = 0;
    silence(&mus);
    silence(&sfx);
    for (i = 0; i < 3; i++) mus.per[i] = sfx.per[i] = 0;
    mus.nctl = sfx.nctl = 4;
    pcm_on = 0;
    rNR10 = 0;
    wave_square();
    invalidate();
    ei();
}

/* ------------------------------------------------------------ the API of the game (ID_SD.C) */
static void play_pcm(u8 d)
{
    di();
    pcm_on = 0;
    wave_dc();
    rNR32 = 0x20;
    rNR33 = 0xFF;
    rNR34 = 0x87;                       /* trigger: constant output, the master volume is the sample */
    rNR51 = 0x44;                       /* the wave channel alone */
    pcm_bnk = pcm_bank[d];
    pcm_ptr = pcm_addr[d];
    pcm_cnt = pcm_len[d];
    pcm_ph = 0;
    pcm_on = 1;
    rTMA = 192;                         /* 8192 Hz at double speed */
    rTIMA = 192;
    rTAC = 0x05;
    rIF = 0;
    rIE |= IE_TIMER;
    ei();
}

u8 SD_PlaySound(u16 sound) __banked
{
    u8 d;
    if (sound >= NUM_SFX)
        return 0;
    d = digi_of[sound];
    if (DigiOn && d != 0xFF && (DigiOn == 2 || digi_voice[d]))
    {
        play_pcm(d);
        cur_sound = sound;
        return 1;
    }
    if (!SoundOn)
        return 0;
    if (sfx.on && sfx_pri[sound] < sfx.pri)
        return 0;
    di();
    silence(&sfx);
    sfx.bank = sfx_bank[sound];
    sfx.p = sfx_addr[sound];
    sfx.pri = sfx_pri[sound];
    sfx.wait = 1;
    sfx.on = 1;
    ei();
    cur_sound = sound;
    return 1;
}

void SD_StopSound(void) __banked
{
    di();
    sfx.on = 0;
    silence(&sfx);
    if (pcm_on)
        pcm_stop();
    ei();
    cur_sound = 0;
}

u16 SD_SoundPlaying(void) __banked
{
    return (sfx.on || pcm_on) ? cur_sound : 0;
}

void SD_WaitSoundDone(void) __banked
{
    u16 t = 0;
    while (SD_SoundPlaying() && t++ < 500)
        wait_vblank();
}

static void music(u8 bank, u16 p)
{
    di();
    silence(&mus);
    mus.bank = bank;
    mus.p = p;
    mus.wait = 1;
    mus.on = p != 0;
    ei();
}

void SD_StartMusic(u16 song) __banked
{
    if (!MusicOn || song >= NUM_SONGS)
        return;
    if (song == cur_song && mus.on)
        return;
    cur_song = (u8)song;
    music(mus_bank[song], mus_addr[song]);
}

void SD_MusicOff(void) __banked
{
    cur_song = 0xFF;
    music(0, 0);
}

void SD_FadeOutMusic(void) __banked
{
    SD_MusicOff();
}
