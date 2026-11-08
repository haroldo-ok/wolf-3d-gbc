/* inter.c - the screens between levels, victory, high scores, the title loop (port of WL_INTER.C) (banked) */
#include "ui.h"
#include <string.h>

void ViewScores(void) B;
void US_ControlPanel(void) B;
void GameLoop(void) B;
void draw_scores(void) B;

u8 ratio_kill[8], ratio_secret[8], ratio_treasure[8];
u16 ratio_time[8];

/* par times of episode 1 in seconds (the secret level and the boss have none) */
static const u16 par_sec[8] = { 90, 120, 120, 210, 180, 180, 150, 150 };

static u8 breath;
static u32 breath_last;
static u16 breath_max;

static void BJ_Breathe(void)
{
    u32 now = get_timecount();
    if (now - breath_last > breath_max)
    {
        breath ^= 1;
        pic_load(PIC_GUY1 + breath, 1, 4, T_FREE);
        breath_last = now;
        breath_max = 35;
    }
}

static void wait_snd(void)
{
    while (SD_SoundPlaying())
    {
        wait_vblank();
        BJ_Breathe();
    }
}

#define ROW_KILL   11
#define ROW_SECRET 12
#define ROW_TREAS  13

static void put_ratio(u8 row, s16 v)
{
    ui_num(10, row, 3, v);
}

/* the ratio counts up to `ratio` with the bonus sounds; TRUE: a button ended it */
static u8 ratio_count(s16 ratio, u8 row, s32 *bonus)
{
    s16 i;
    for (i = 0; i <= ratio; i++)
    {
        put_ratio(row, i);
        if (!(i % 10))
            SD_PlaySound(ENDBONUS1SND);
        wait_snd();
        wait_vblank();
        if (IN_CheckAck())
            return 1;
    }
    if (ratio == 100)
    {
        vid_wait(30);
        SD_StopSound();
        *bonus += 10000;
        ui_num(13, 7, 7, *bonus);
        SD_PlaySound(PERCENT100SND);
    }
    else if (!ratio)
    {
        vid_wait(30);
        SD_StopSound();
        SD_PlaySound(NOBONUSSND);
    }
    else
        SD_PlaySound(ENDBONUS2SND);
    wait_snd();
    return 0;
}

static void inter_frame(const char *title)
{
    char b[8];
    scr_open(UK_INTER);
    ui_ctext(0, title, P_YEL);
    ui_text(1, 11, "KILL", P_TEXT);
    ui_text(14, 11, "%", P_TEXT);
    ui_text(1, 12, "SECRET", P_TEXT);
    ui_text(14, 12, "%", P_TEXT);
    ui_text(1, 13, "TREASURE", P_TEXT);
    ui_text(14, 13, "%", P_TEXT);
    (void)b;
}

void LevelCompleted(void) B
{
    s16 mapon = gamestate.mapon, i, kr = 0, sr = 0, tr = 0, min, sec;
    s32 bonus, timeleft = 0;
    u8 abort = 0;
    char b[16];
    ui_kind = UK_INTER;
    scr_open(UK_INTER);
    SD_StartMusic(ENDLEVEL_MUS);
    breath = 0;
    breath_last = get_timecount();
    breath_max = 10;
    if (mapon < 8)
    {
        strcpy(b, "FLOOR ");
        ui_itoa(b + 6, mapon + 1);
        ui_ctext(0, b, P_YEL);
        ui_ctext(1, "COMPLETED", P_YEL);
        pic_load(PIC_GUY1, 1, 4, T_FREE);
        ui_text(9, 4, "TIME", P_TEXT);
        ui_text(9, 5, "PAR", P_TEXT);
        ui_text(8, 7, "BONUS", P_TEXT);
        ui_text(1, 11, "KILL", P_TEXT);
        ui_text(14, 11, "%", P_TEXT);
        ui_text(1, 12, "SECRET", P_TEXT);
        ui_text(14, 12, "%", P_TEXT);
        ui_text(1, 13, "TREASURE", P_TEXT);
        ui_text(14, 13, "%", P_TEXT);
        ui_text(1, 15, "SCORE", P_TEXT);
        ui_time(14, 5, par_sec[mapon] / 60, par_sec[mapon] % 60);
        sec = (s16)(gamestate.TimeCount / 70);
        if (sec > 99 * 60)
            sec = 99 * 60;
        if (gamestate.TimeCount < (s32)par_sec[mapon] * 70)
            timeleft = par_sec[mapon] - sec;
        min = sec / 60;
        sec %= 60;
        ui_time(14, 4, min, (u8)sec);
        ui_num(13, 7, 7, 0);
        ui_num(12, 15, 8, gamestate.score);
        put_ratio(ROW_KILL, 0);
        put_ratio(ROW_SECRET, 0);
        put_ratio(ROW_TREAS, 0);
        scr_show();

        if (gamestate.killtotal)
            kr = (s16)((gamestate.killcount * 100L) / gamestate.killtotal);
        if (gamestate.secrettotal)
            sr = (s16)((gamestate.secretcount * 100L) / gamestate.secrettotal);
        if (gamestate.treasuretotal)
            tr = (s16)((gamestate.treasurecount * 100L) / gamestate.treasuretotal);

        bonus = timeleft * 500;
        IN_ClearKeys();
        if (bonus)
        {
            for (i = 0; i <= timeleft; i++)
            {
                ui_num(13, 7, 7, (s32)i * 500);
                if (!(i % 50))
                    SD_PlaySound(ENDBONUS1SND);
                wait_snd();
                wait_vblank();
                if (IN_CheckAck())
                {
                    abort = 1;
                    break;
                }
            }
            if (!abort)
            {
                SD_PlaySound(ENDBONUS2SND);
                wait_snd();
            }
        }
        if (!abort) abort = ratio_count(kr, ROW_KILL, &bonus);
        if (!abort) abort = ratio_count(sr, ROW_SECRET, &bonus);
        if (!abort) abort = ratio_count(tr, ROW_TREAS, &bonus);

        put_ratio(ROW_KILL, kr);
        put_ratio(ROW_SECRET, sr);
        put_ratio(ROW_TREAS, tr);
        bonus = timeleft * 500 + (kr == 100 ? 10000L : 0) + (sr == 100 ? 10000L : 0) + (tr == 100 ? 10000L : 0);
        GivePoints(bonus);
        ui_num(13, 7, 7, bonus);
        ratio_kill[mapon] = (u8)kr;
        ratio_secret[mapon] = (u8)sr;
        ratio_treasure[mapon] = (u8)tr;
        ratio_time[mapon] = min * 60 + sec;
    }
    else
    {
        ui_ctext(0, "SECRET FLOOR", P_YEL);
        ui_ctext(1, "COMPLETED", P_YEL);
        pic_load(PIC_GUY1, 1, 4, T_FREE);
        ui_text(1, 15, "SCORE", P_TEXT);
        scr_show();
        GivePoints(15000);
    }
    ui_num(12, 15, 8, gamestate.score);
    IN_ClearKeys();
    set_timecount(0);
    breath_last = 0;
    for (;;)
    {
        wait_vblank();
        BJ_Breathe();
        if (IN_CheckAck())
            break;
    }
    scr_leave();
}

void Victory(void) B
{
    s16 i, min, kr = 0, sr = 0, tr = 0;
    s32 sec = 0;
    scr_open(UK_INTER);
    SD_StartMusic(URAHERO_MUS);
    for (i = 0; i < 8; i++)
    {
        sec += ratio_time[i];
        kr += ratio_kill[i];
        sr += ratio_secret[i];
        tr += ratio_treasure[i];
    }
    kr /= 8;
    sr /= 8;
    tr /= 8;
    min = (s16)(sec / 60);
    sec %= 60;
    if (min > 99)
        min = sec = 99;
    ui_ctext(0, "YOU WIN!", P_YEL);
    pic_load(PIC_BJWIN, 1, 2, T_FREE);
    ui_text(9, 2, "TOTAL TIME", P_TEXT);
    ui_time(12, 3, min, (u8)sec);
    ui_text(9, 5, "AVERAGES", P_TEXT);
    ui_text(9, 6, "KILL", P_TEXT);
    ui_text(9, 7, "SECRET", P_TEXT);
    ui_text(9, 8, "TREASURE", P_TEXT);
    ui_num(16, 6, 3, kr);
    ui_num(16, 7, 3, sr);
    ui_num(16, 8, 3, tr);
    ui_text(19, 6, "%", P_TEXT);
    ui_text(19, 7, "%", P_TEXT);
    ui_text(19, 8, "%", P_TEXT);
    scr_show();
    IN_ClearKeys();
    IN_Ack();
    scr_leave();
    EndText();
}

/* the level is loading: a bar fills */
void Inter_GetPsyched(void) B
{
    u8 i;
#ifdef NOPSY
    return;
#endif
    scr_open(UK_MENU);
    pic_load(PIC_PSYCHED, 1, 4, T_FREE);
    ui_box(1, 10, 18, 3);
    scr_show();
    for (i = 0; i < 16; i++)
    {
        ui_put(2 + i, 11, T_SOLID1, P_YEL);
        vid_wait(3);
    }
    vid_wait(25);
    scr_leave();
}

/* the view turns red when the player has died */
void Inter_FadeToColor(u8 color) B
{
    (void)color;
    vid_flash(1, 0, 0, 3);
}

/* ---------------------------------------------------------- high scores */
#define MAXSCORES   7
#define NAMELEN     15
typedef struct
{
    char name[NAMELEN + 1];
    u16 completed;
    s32 score;
} HighScore;
#define Scores ((HighScore *)CR_SCRATCH)      /* cartridge RAM bank 0 work space: keeps WRAM0 for the stack */


static const char *const defname[MAXSCORES] =
{
    "id software-'92", "Adrian Carmack", "John Carmack", "Kevin Cloud", "Tom Hall", "John Romero", "Jay Wilbur"
};

void Scores_Defaults(void) B
{
    u8 i;
    for (i = 0; i < MAXSCORES; i++)
    {
        memset(Scores[i].name, 0, NAMELEN + 1);
        strcpy(Scores[i].name, defname[i]);
        Scores[i].completed = 1;
        Scores[i].score = 10000;
    }
}

void *Scores_Ptr(void) B { return Scores; }
u16 Scores_Size(void) B { return sizeof(HighScore) * MAXSCORES; }

void draw_scores(void) B
{
    u8 i;
    char b[12];
    scr_open(UK_MENU);
    ui_ctext(0, "HIGH SCORES", P_OUTY);
    for (i = 0; i < MAXSCORES; i++)
    {
        u8 row = 2 + 2 * i;
        ui_text(0, row, Scores[i].name, P_OUT);
        ui_text(16, row, "L", P_OUT);
        ui_pad(b, Scores[i].completed, 2);
        ui_text(17, row, b, P_OUT);
        ui_pad(b, Scores[i].score, 8);
        ui_text(12, row + 1, b, P_OUTY);
    }
}

static const char name_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-'!";

static void name_entry(char *name)
{
    char buf[NAMELEN + 1];
    u8 pos = 0, i, anim = 0, edge, redraw = 1, n = sizeof(name_chars) - 1;
    memset(buf, ' ', NAMELEN);
    buf[NAMELEN] = 0;
    scr_open(UK_MENU);
    ui_ctext(3, "NEW HIGH SCORE!", P_OUTY);
    ui_ctext(6, "Enter your name:", P_OUT);
    ui_text(2, 9, buf, P_TEXT);
    ui_ctext(14, "UP/DOWN letter", P_OUT);
    ui_ctext(15, "LEFT/RIGHT move", P_OUT);
    ui_ctext(16, "A done  B delete", P_OUT);
    scr_show();
    IN_ClearKeys();
    for (;;)
    {
        if (redraw)
        {
            ui_text(2, 9, buf, P_TEXT);
            redraw = 0;
        }
        wait_vblank();
        IN_Update();
        anim++;
        ui_pal(2 + pos, 9, 1, (anim >> 3) & 1 ? P_HI : P_YEL);
        edge = pad_now & ~pad_last;
        if (edge & (PAD_UP | PAD_DOWN))
        {
            for (i = 0; i < n && name_chars[i] != buf[pos]; i++) ;
            if (i == n) i = 0;
            i = (edge & PAD_UP) ? (i + 1) % n : (i + n - 1) % n;
            buf[pos] = name_chars[i];
            redraw = 1;
        }
        if ((edge & PAD_RIGHT) && pos < NAMELEN - 1)
        {
            ui_pal(2 + pos, 9, 1, P_TEXT);
            pos++;
        }
        if ((edge & PAD_LEFT) && pos > 0)
        {
            ui_pal(2 + pos, 9, 1, P_TEXT);
            pos--;
        }
        if (pad_new & PAD_2)
        {
            buf[pos] = ' ';
            if (pos > 0) pos--;
            redraw = 1;
        }
        if (pad_new & (PAD_1 | PAD_PAUSE))
            break;
    }
    for (i = NAMELEN; i > 0 && buf[i - 1] == ' '; i--) ;
    buf[i] = 0;
    strcpy(name, buf);
    scr_leave();
}

void CheckHighScore(s32 score, u16 other) B
{
    u8 i, j, rank = MAXSCORES;
    for (i = 0; i < MAXSCORES; i++)
        if (score > Scores[i].score)
        {
            rank = i;
            break;
        }
    if (rank < MAXSCORES)
    {
        for (j = MAXSCORES - 1; j > rank; j--)
            Scores[j] = Scores[j - 1];
        memset(Scores[rank].name, 0, NAMELEN + 1);
        Scores[rank].score = score;
        Scores[rank].completed = other;
        SD_StartMusic(ROSTER_MUS);
        name_entry(Scores[rank].name);
        if (!Scores[rank].name[0])
            strcpy(Scores[rank].name, "Anonymous");
        Scores_Store();
    }
    ViewScores();
}

void ViewScores(void) B
{
    SD_StartMusic(ROSTER_MUS);
    draw_scores();
    scr_show();
    IN_ClearKeys();
    IN_Ack();
    scr_leave();
}

/* ------------------------------------------------------------ title loop */
void Quit_ToTitle(void) B
{
    __asm__("di\n jp 0");
}

void RestoreGameScreen(void) B
{
    vid_init_game();
    DrawStatusBar();
    ThreeDRefresh();
    vid_fade_in();
}

static void show_pic(u8 id)
{
    scr_open(UK_MENU);
    pic_load(id, 0, 0, 0);
    scr_show();
}

/* PG-13, then the title, credits and scores until a button is pressed; then the menu */
/* the title in hi-colour (tools/gb_hicolor.py, hc_frame in hw_a.s): the palettes change on every line, so the CPU
 * is busy for the whole frame; returns 1 when a button was pressed within tics */
extern u8 hc_bank;
extern volatile u8 pal_pending;
extern u16 hc_stream;
void hc_frame(void);

static u8 title_hicolor(u16 tics)
{
    u8 r, hit = 0;
    u32 t0;
    const u8 *base = (const u8 *)(HC_TITLE_ADDR + 16);
    scr_open(UK_MENU);                          /* LCD off, sprites off, the BG maps cleared */
    rom_to_vram(HC_TITLE_BANK, base, 0, 0x8000, 256 * 16);
    rom_to_vram(HC_TITLE_BANK, base + 256 * 16, 1, 0x8000, 104 * 16);
    for (r = 0; r < 18; r++)
    {
        rom_to_vram(HC_TITLE_BANK, base + 360 * 16 + r * 20, 0, 0x9800 + (u16)r * 32, 20);
        rom_to_vram(HC_TITLE_BANK, base + 360 * 16 + 360 + r * 20, 1, 0x9800 + (u16)r * 32, 20);
    }
    hc_bank = HC_TITLE_BANK;
    hc_stream = HC_TITLE_ADDR + 16 + 360 * 16 + 720;
    pal_pending = 0;
    lcd_on(LCDC_BGON | LCDC_BGDATA8000);
    t0 = get_timecount();
    IN_ClearKeys();
    do
    {
        hc_frame();
        if (IN_CheckAck())
        {
            hit = 1;
            break;
        }
    } while (get_timecount() - t0 < tics);
    vid_set_palette_black();
    wait_vblank();
    wait_vblank();
    return hit;
}

void title_loop(void) B
{
    u8 first = 1;
    ingame = FALSE;
    Scores_Defaults();
    Scores_Load();
    for (;;)
    {
        if (first)
        {
            show_pic(PIC_PG13);
            ui_wait(70 * 3);
            scr_leave();
            first = 0;
        }
        SD_StartMusic(NAZI_NOR_MUS);
        if (title_hicolor(70 * 9)) goto menu;
        scr_leave();
        show_pic(PIC_CREDITS);
        if (ui_wait(70 * 6)) goto menu;
        scr_leave();
        draw_scores();
        scr_show();
        if (ui_wait(70 * 6)) goto menu;
        scr_leave();
        continue;
menu:
        scr_leave();
        US_ControlPanel();
        if (startgame || loadedgame)
        {
            SD_MusicOff();
            GameLoop();
            ingame = FALSE;
            first = 0;
        }
    }
}
