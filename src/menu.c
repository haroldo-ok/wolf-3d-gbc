/* menu.c - the selection loop and the control panel (port of WL_MENU.C) (banked) */
#include "ui.h"
#include <string.h>

extern u8 SoundOn, MusicOn, DigiOn;
void ViewScores(void) B;

static u8 item_pal(const MenuItem *it)
{
    return (it->flags & 1) ? P_DIM : (it->flags & 2) ? P_YEL : P_TEXT;
}

static u8 curx(const MenuItem *it)
{
    return (it->flags & 4) ? (u8)((it->col - 8) * 8) : (it->flags & 8) ? (u8)((it->col - 2) * 8) : (u8)((it->col - 3) * 8);
}

static void hilite(const MenuItem *it, u8 on)
{
    ui_pal(it->col, it->row, (u8)strlen(it->text), on ? P_HI : item_pal(it));
}

static void draw_items(const MenuItem *it, u8 n)
{
    u8 i;
    for (i = 0; i < n; i++)
        ui_text(it[i].col, it[i].row, it[i].text, item_pal(&it[i]));
}

s8 menu_run(const MenuItem *it, u8 n, u8 start, void (*onmove)(u8)) B
{
    u8 cur = start, i, frame = 0, anim = 0, hold = 0, edge;
    s8 dir;
    draw_items(it, n);
    for (i = 0; i < n && (it[cur].flags & 1); i++)
        cur = (cur + 1) % n;
    hilite(&it[cur], 1);
    ui_cursor(curx(&it[cur]), it[cur].row * 8, 0);
    if (onmove)
        onmove(cur);
    IN_ClearKeys();
    for (;;)
    {
        wait_vblank();
        IN_Update();
        anim++;
        if ((anim & 15) == 0)
        {
            frame ^= 1;
            ui_cursor(curx(&it[cur]), it[cur].row * 8, frame);
        }
        edge = pad_now & ~pad_last;
        dir = 0;
        if (edge & (PAD_UP | PAD_DOWN))
        {
            hold = 0;
            dir = (edge & PAD_UP) ? -1 : 1;
        }
        else if (pad_now & (PAD_UP | PAD_DOWN))
        {
            hold++;
            if (hold > 20 && ((hold - 20) & 3) == 0)
                dir = (pad_now & PAD_UP) ? -1 : 1;
        }
        else
            hold = 0;
        if (dir)
        {
            hilite(&it[cur], 0);
            i = n;
            do
            {
                cur = (u8)((s8)cur + dir + (s8)n) % n;
            } while (--i && (it[cur].flags & 1));
            hilite(&it[cur], 1);
            ui_cursor(curx(&it[cur]), it[cur].row * 8, frame);
            if (onmove)
                onmove(cur);
            SD_PlaySound(MOVEGUN2SND);
        }
        if (pad_new & PAD_1)
        {
            SD_PlaySound(SHOOTSND);
            return (s8)cur;
        }
        if (pad_new & (PAD_2 | PAD_PAUSE))
        {
            SD_PlaySound(ESCPRESSEDSND);
            return -1;
        }
    }
}

/* ------------------------------------------------------------ new game */
static void face(u8 i)
{
    pic_load(PIC_FACE0 + i, 8, 13, T_FREE);
}

static u8 confirm(const char *l1, const char *l2)
{
    static const MenuItem it[2] = { { 7, 12, 0, "Yes" }, { 7, 14, 0, "No" } };
    s8 w;
    scr_open(UK_MENU);
    ui_box(1, 5, 18, 12);
    ui_ctext(7, l1, P_TEXT);
    ui_ctext(8, l2, P_TEXT);
    scr_show();
    w = menu_run(it, 2, 1, 0);
    scr_leave();
    return w == 0;
}

static u8 cp_new_game(void)
{
    static const MenuItem it[4] = { { 2, 4, 8, "Can I play, Daddy?" }, { 2, 6, 8, "Don't hurt me." },
                                    { 2, 8, 8, "Bring 'em on!" }, { 2, 10, 8, "Death incarnate!" } };
    s8 w;
    if (ingame && !confirm("Abort the game and", "start a new one?"))
        return 0;
    scr_open(UK_MENU);
    ui_ctext(1, "How tough are you?", P_OUTY);
    ui_box(0, 2, 20, 11);
    ui_fill(7, 12, 6, 6, 0, P_OUT);
    scr_show();
    w = menu_run(it, 4, 2, face);
    scr_leave();
    if (w < 0)
        return 0;
    NewGame(w, 0);
    startgame = TRUE;
    return 1;
}

/* --------------------------------------------------------------- sound */
static const MenuItem snd_it[7] =
{
    { 8, 2, 4, "None" }, { 8, 3, 4, "GBC sound" },
    { 8, 7, 4, "None" }, { 8, 8, 4, "Voices only" }, { 8, 9, 4, "All sounds" },
    { 8, 13, 4, "None" }, { 8, 14, 4, "GBC sound" },
};

static void sound_marks(void)
{
    u8 sel[7], i;
    sel[0] = !SoundOn; sel[1] = SoundOn;
    sel[2] = DigiOn == 0; sel[3] = DigiOn == 1; sel[4] = DigiOn == 2;
    sel[5] = !MusicOn; sel[6] = MusicOn;
    for (i = 0; i < 7; i++)
    {
        ui_text(4, snd_it[i].row, sel[i] ? "[X]" : "[ ]", sel[i] ? P_YEL : P_TEXT);
    }
}

static void cp_sound(void)
{
    s8 w;
    u8 pos = 0;
    scr_open(UK_MENU);
    pic_load(PIC_FXT, 1, 0, T_FREE);
    ui_fill(0, 2, 20, 2, 0, P_TEXT);
    pic_load(PIC_DIGIT, 1, 5, T_FREE + 40);
    ui_fill(0, 7, 20, 3, 0, P_TEXT);
    pic_load(PIC_MUSICT, 1, 11, T_FREE + 80);
    ui_fill(0, 13, 20, 2, 0, P_TEXT);
    scr_show();
    sound_marks();
    for (;;)
    {
        w = menu_run(snd_it, 7, pos, 0);
        if (w < 0)
            break;
        pos = (u8)w;
        switch (w)
        {
        case 0: SoundOn = 0; SD_StopSound(); break;
        case 1: SoundOn = 1; SD_PlaySound(SHOOTSND); break;
        case 2: DigiOn = 0; SD_StopSound(); break;
        case 3: DigiOn = 1; SD_PlaySound(GUTENTAGSND); break;
        case 4: DigiOn = 2; SD_PlaySound(ATKPISTOLSND); break;
        case 5: if (MusicOn) { MusicOn = 0; SD_MusicOff(); } break;
        case 6: if (!MusicOn) { MusicOn = 1; SD_StartMusic(WONDERIN_MUS); } break;
        }
        sound_marks();
    }
    scr_leave();
}

/* ------------------------------------------------------- load and save */
static const char *const diffname[4] = { "BABY", "EASY", "NORMAL", "HARD" };
static const MenuItem slot_it[3] = { { 5, 4, 0, "SLOT 1" }, { 5, 9, 0, "SLOT 2" }, { 5, 14, 0, "SLOT 3" } };

static void slot_text(u8 k)
{
    SaveInfo si;
    char b[24], n[12];
    u8 row = slot_it[k].row;
    ui_text(2, row + 1, "                  ", P_TEXT);
    ui_text(2, row + 2, "                  ", P_TEXT);
    if (!Save_Info(k, &si))
    {
        ui_text(5, row + 1, "- EMPTY -", P_TEXT);
        return;
    }
    ui_text(1, row + 1, "FLOOR", P_TEXT);
    ui_itoa(n, si.floor);
    ui_text(7, row + 1, n, P_TEXT);
    ui_text(10, row + 1, diffname[si.diff & 3], P_TEXT);
    ui_pad(b, si.score, 8);
    ui_text(1, row + 2, b, P_TEXT);
    ui_text(10, row + 2, "LIVES", P_TEXT);
    ui_itoa(n, si.lives);
    ui_text(16, row + 2, n, P_TEXT);
}

static void slots_draw(const char *title)
{
    u8 k;
    scr_open(UK_MENU);
    ui_ctext(1, title, P_OUTY);
    for (k = 0; k < 3; k++)
        ui_box(0, slot_it[k].row - 1, 20, 5);
    scr_show();
    for (k = 0; k < 3; k++)
        slot_text(k);
}

static u8 cp_load(void)
{
    s8 w;
    SaveInfo si;
    slots_draw("LOAD GAME");
    for (;;)
    {
        w = menu_run(slot_it, 3, 0, 0);
        if (w < 0)
        {
            scr_leave();
            return 0;
        }
        if (!Save_Info((u8)w, &si))
            continue;
        ui_cursor_off();
        scr_leave();
        if (Save_Read((u8)w))
        {
            loadedgame = TRUE;
            return 1;
        }
        slots_draw("LOAD GAME");
        ui_text(1, 17, "CAN'T LOAD", P_OUTY);
    }
}

static void cp_save(void)
{
    s8 w;
    slots_draw("SAVE GAME");
    for (;;)
    {
        w = menu_run(slot_it, 3, 0, 0);
        if (w < 0)
            break;
        ui_text(5, slot_it[w].row, "SAVING...", P_YEL);
        ui_cursor_off();
        if (Save_Write((u8)w))
        {
            slot_text((u8)w);
            ui_text(5, slot_it[w].row, "SLOT       ", P_YEL);
            ui_wait(35);
        }
        else
            ui_text(5, slot_it[w].row, "NOT SAVED", P_YEL);
        ui_pal(5, slot_it[w].row, 9, P_TEXT);
    }
    scr_leave();
}

/* ---------------------------------------------------- read this, the end */
/* pages are 256 x 192: the pad pans, A next page, SELECT previous, B leaves */
static void show_pages(u8 first, u8 count)
{
    u8 page = 0, x = 0, y = 0, edge, redraw = 1;
    IN_ClearKeys();
    for (;;)
    {
        if (redraw)
        {
            scr_open(UK_MENU);
            pic_load(first + page, 0, 0, 0);
            scr_show();
            x = y = 0;
            redraw = 0;
        }
        wait_vblank();
        IN_Update();
        edge = pad_new;
        if (edge & PAD_2)
            break;
        if ((edge & PAD_1) && page + 1 < count)
        {
            scr_leave();
            page++;
            redraw = 1;
        }
        else if ((edge & PAD_SEL) && page)
        {
            scr_leave();
            page--;
            redraw = 1;
        }
        else
        {
            if ((pad_now & PAD_LEFT) && x) x = x > 2 ? x - 2 : 0;
            if ((pad_now & PAD_RIGHT) && x < 96) x = x < 94 ? x + 2 : 96;
            if ((pad_now & PAD_UP) && y) y = y > 2 ? y - 2 : 0;
            if ((pad_now & PAD_DOWN) && y < 48) y = y < 46 ? y + 2 : 48;
            rSCX = x;
            rSCY = y;
        }
    }
    scr_leave();
}

void HelpScreens(void) B
{
    show_pages(PIC_HELP0, NHELP);
}

void EndText(void) B
{
    show_pages(PIC_END0, NEND);
}

/* --------------------------------------------------------- control panel */
void US_ControlPanel(void) B
{
    static const MenuItem main_it[8] =
    {
        { 6, 7, 0, "New Game" }, { 6, 8, 0, "Sound" }, { 6, 9, 0, "Load Game" }, { 6, 10, 0, "Save Game" },
        { 6, 11, 2, "Read This!" }, { 6, 12, 0, "View Scores" }, { 6, 13, 0, "" }, { 6, 14, 0, "Quit" },
    };
    MenuItem it[8];
    s8 w;
    u8 pos = 0, done = 0;
    SD_StartMusic(WONDERIN_MUS);
    for (;;)
    {
        memcpy(it, main_it, sizeof(it));
        it[3].flags = ingame ? 0 : 1;
        it[6].text = ingame ? "Back to Game" : "Back to Title";
        scr_open(UK_MENU);
        pic_load(PIC_OPTIONS, 0, 0, T_FREE);
        ui_box(0, 6, 20, 11);
        scr_show();
        w = menu_run(it, 8, pos, 0);
        scr_leave();
        if (w < 0 || w == 6)
            break;
        pos = (u8)w;
        switch (w)
        {
        case 0: if (cp_new_game()) done = 1; break;
        case 1: cp_sound(); break;
        case 2: if (cp_load()) done = 1; break;
        case 3: cp_save(); break;
        case 4: HelpScreens(); break;
        case 5: ViewScores(); break;
        case 7:
            if (confirm("Really quit the", "game?"))
                Quit_ToTitle();
            break;
        }
        if (done)
            break;
    }
    if (ingame && !done)
        RestoreGameScreen();
}
