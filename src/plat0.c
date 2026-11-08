/* plat0.c - bank 0: timer and fatal errors */
#include "game/def.h"
#include "gbc.h"

u32 get_timecount(void)
{
    u32 t;
    di();
    t = time_count;
    ei();
    return t;
}

void set_timecount(u32 t)
{
    di();
    time_count = t;
    ei();
}

char dbg_quitbuf[48];       /* the tests read this symbol after a fatal error */
void Quit(const char *error)
{
    u8 i;
    for (i = 0; error && i < 47 && error[i]; i++)
        dbg_quitbuf[i] = error[i];
    dbg_quitbuf[i] = 0;
    di();
    rLCDC = 0;
    for (;;)
        halt();
}

void snd_init(void) __banked;
void SD_Startup(void) { snd_init(); }
