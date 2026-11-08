#include "game/def.h"
#include "ui.h"

void mul_init(void) __banked;

void NewGame(s16 difficulty, s16 episode) B;
void GameLoop(void) B;
extern boolean startgame;

#ifdef DIRECT
extern gametype gamestate;
volatile u8 dbg_go, dbg_startmap, dbg_god;   /* the test ROM: start at this level once the test says go */
#endif

void main(void)
{
    hw_init();
    mul_init();
    SD_Startup();
#ifdef DIRECT
    for (;;)
    {
#ifdef AUTOGO
        dbg_go = 1;
#endif
        while (!dbg_go) nop();
        NewGame(gd_medium, 0);
        gamestate.mapon = dbg_startmap;
        startgame = TRUE;
        GameLoop();
    }
#else
    for (;;)
        title_loop();
#endif
}
