/* input.c - the pad (banked) */
#include "game/def.h"

u8 pad_now, pad_last;

void IN_Update(void) B
{
    pad_last = pad_now;
    pad_now = pad_read();
    pad_new = pad_now & ~pad_last;
}

void IN_ClearKeys(void) B
{
    IN_Update();
    while (pad_now & (PAD_1 | PAD_2 | PAD_PAUSE))
    {
        wait_vblank();
        IN_Update();
    }
    pad_last = pad_now;
    pad_new = 0;
    di();                               /* forget what the frame interrupt saw (e.g. the A that left a menu) */
    pad_acc[0] = pad_acc[1] = pad_acc[2] = pad_acc[3] = 0;
    pad_nacc = 0;
    pad_or = 0;
    ei();
}

u8 IN_CheckAck(void) B
{
    IN_Update();
    return (pad_new & (PAD_1 | PAD_2 | PAD_PAUSE)) != 0;
}

void IN_Ack(void) B
{
    while (!IN_CheckAck())
        wait_vblank();
}

u8 IN_UserInput(u32 delay) B
{
    u32 lasttime = get_timecount();
    do
    {
        wait_vblank();
        if (IN_CheckAck())
            return 1;
    } while (get_timecount() - lasttime < delay);
    return 0;
}
