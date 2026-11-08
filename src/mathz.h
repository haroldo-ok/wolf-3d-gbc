#ifndef MATHZ_H
#define MATHZ_H
#include "hw.h"
typedef s32 fixed;
fixed FixedByFrac(fixed a, fixed b);     /* a (16.16) * b (fraction, |b| < 65536) */
u16  udiv32_16(u32 n, u16 d);            /* n < d << 16 */
long smul16(int a, int b);
u32  umul16(u16 a, u16 b);              /* 16x16 -> 32 */
#define umul16w(a, b) umul16((u16)(a), (u16)(b))               /* signed 16x16 -> 32 */
fixed fsin(s16 a);
fixed fcos(s16 a);
u16  random16(void);
void random_seed(u16 s);
#endif
