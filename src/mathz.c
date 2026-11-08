/* mathz.c - bank 0: fixed point and integer helpers (sm83) */
#include "hw.h"
#include "mathz.h"

u32 umul16(u16 a, u16 b);                       /* mathz_a.s */
extern u32 dv_n;
extern u16 dv_d;
u16 udiv_nd(void);

/* FixedByFrac: a 16.16 number times a fraction; like the original only the fraction's low 16 bits of magnitude count */
fixed FixedByFrac(fixed a, fixed b)
{
    u8 neg = 0;
    u32 ua, r;
    u16 ub, hi, lo;
    if (a < 0) { ua = -a; neg = 1; } else ua = a;
    if (b < 0) { ub = (u16)(-b); neg ^= 1; } else ub = (u16)b;
    hi = ((u16 *)&ua)[1];
    lo = ((u16 *)&ua)[0];
    if (lo)
    {
        r = umul16(lo, ub);
        lo = ((u16 *)&r)[1];
    }
    r = hi ? umul16(hi, ub) + lo : lo;
    return neg ? -(fixed)r : (fixed)r;
}

u16 udiv32_16(u32 n, u16 d)
{
    dv_n = n;
    dv_d = d;
    return udiv_nd();
}

/* smul16: mathz_a.s */

static u16 rnd_state = 0xACE1;
u16 random16(void)
{
    u16 x = rnd_state;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    rnd_state = x;
    return x;
}

void random_seed(u16 s)
{
    rnd_state = s ? s : 0xACE1;
}

extern const u16 sin_q[91];
fixed fsin(s16 a)
{
    while (a >= 360) a -= 360;
    while (a < 0) a += 360;
    if (a <= 90) return sin_q[a];
    if (a <= 180) return sin_q[180 - a];
    if (a <= 270) return -(fixed)sin_q[a - 180];
    return -(fixed)sin_q[360 - a];
}

fixed fcos(s16 a)
{
    return fsin(a + 90);
}
