#ifndef TIMER_TIMERDOS_H
#define TIMER_TIMERDOS_H

#include "timer.h"

extern bool TimerDOS_Init(void);
extern void TimerDOS_Uninit(void);
extern bool TimerDOS_TakeInputTick(void);
extern void TimerDOS_FlushInputTicks(void);
extern void TimerDOS_Idle(void);
extern int TimerDOS_GameHz(void);

#endif
