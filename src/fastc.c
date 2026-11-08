/* fastc.c - bank 0: small helpers of the game logic (the Master System build had them in assembly) */
#include "game/def.h"

/* the actor of an actorat byte (0x40 | id, id bit 6 in bit 7), 0 when v is not an actor */
objtype *act_dec(u8 v)
{
    u8 id;
    if (!(v & 0x40))
        return NULL;
    id = (v & 0x3F) | ((v & 0x80) >> 1);
    return &objlist[id];
}

s32 mul_tics(u8 t, u16 v)
{
    return (s32)umul16w(t, v);
}

/* the fast paths of the original are not needed here: the caller falls back to the C */
u8 tpath_fast(objtype *ob)
{
    (void)ob;
    return 0;
}

u8 trymove_a(objtype *ob)
{
    (void)ob;
    return 2;
}
