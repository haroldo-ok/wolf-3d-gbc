#ifndef SND_H
#define SND_H
#include "hw.h"
extern u8 snd_ticks;                    /* 70 Hz ticks to run at the next frame interrupt (hw.c) */
void snd_frame(void) __banked;          /* the frame interrupt: music, effects, the sound chip (snd.c) */
void snd_init(void) __banked;
#endif
