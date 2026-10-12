/* timer_dos.c
 *
 * MS-DOS timers on Allegro 4.  The interrupt handlers only bump
 * counters; everything else happens in the main loop.  Keep them that
 * way: slow ISRs show up on real hardware and 86Box, not in DOSBox.
 */

#include <allegro.h>
#include <assert.h>
#include <dos.h>
#include <dpmi.h>
#include "errorlog.h"

#include "perf_dos.h"
#include "timer_dos.h"

#include "../audio/audio.h"
#include "../config.h"
#include "../enhancement.h"
#include "../net/net.h"
#include "../video/video_dos.h"

enum GameSpeed {
	GAMESPEED_SLOWEST   = 0,
	GAMESPEED_SLOW      = 1,
	GAMESPEED_NORMAL    = 2,
	GAMESPEED_FAST      = 3,
	GAMESPEED_FASTEST   = 4,

	GAMESPEED_MAX
};

static const int s_game_speed_hz[GAMESPEED_MAX] = {
	30, 45, 60, 90, 120
};

/* Written by the timer ISRs. */
static volatile int32_t s_count[2];
static volatile int32_t s_queued[2];
static volatile int32_t s_input_queued;
static volatile bool s_started[2];

/* Main loop only. */
static int64_t s_count_base[2];
static bool s_queue_registered;
static int s_game_hz = 60;

static void
TimerDOS_GUIHandler(void)
{
	/* Keeps the 55 ms performance clock from wrapping unseen. */
	Perf_Now();

	if (s_started[TIMER_GUI]) {
		s_count[TIMER_GUI]++;
		s_queued[TIMER_GUI]++;
		s_input_queued++;
	}
}
END_OF_STATIC_FUNCTION(TimerDOS_GUIHandler)

static void
TimerDOS_GameHandler(void)
{
	if (s_started[TIMER_GAME]) {
		s_count[TIMER_GAME]++;
		s_queued[TIMER_GAME]++;
	}
}
END_OF_STATIC_FUNCTION(TimerDOS_GameHandler)

static void
TimerDOS_Install(enum TimerType timer, int hz)
{
	if (timer == TIMER_GUI) {
		install_int_ex(TimerDOS_GUIHandler, BPS_TO_TIMER(hz));
	} else {
		install_int_ex(TimerDOS_GameHandler, BPS_TO_TIMER(hz));
		s_game_hz = hz;
	}
}

bool
TimerDOS_Init(void)
{
	LOCK_VARIABLE(s_count);
	LOCK_VARIABLE(s_queued);
	LOCK_VARIABLE(s_input_queued);
	LOCK_VARIABLE(s_started);
	LOCK_FUNCTION(TimerDOS_GUIHandler);
	LOCK_FUNCTION(TimerDOS_GameHandler);

	if (install_timer() != 0) {
		Error("install_timer() failed.\n");
		return false;
	}

	TimerDOS_Install(TIMER_GUI, s_game_speed_hz[GAMESPEED_NORMAL]);
	TimerDOS_Install(TIMER_GAME, s_game_speed_hz[GAMESPEED_NORMAL]);
	return true;
}

void
TimerDOS_Uninit(void)
{
	remove_int(TimerDOS_GUIHandler);
	remove_int(TimerDOS_GameHandler);
}

/**
 * Atomically take the pending count from an ISR-written counter.
 */
static int32_t
TimerDOS_Take(volatile int32_t *counter)
{
	disable();
	const int32_t n = *counter;
	*counter = 0;
	enable();
	return n;
}

/**
 * Consume one GUI tick destined for the input/menu event loop.
 * @return true if a GUI tick was pending.
 */
bool
TimerDOS_TakeInputTick(void)
{
	bool taken = false;

	disable();
	if (s_input_queued > 0) {
		s_input_queued--;
		taken = true;
	}
	enable();

	return taken;
}

void
TimerDOS_FlushInputTicks(void)
{
	TimerDOS_Take(&s_input_queued);
}

/* Game ticks per second wanted at the current game speed. */
int
TimerDOS_GameHz(void)
{
	return s_game_hz;
}

void
TimerDOS_Idle(void)
{
	VideoDOS_PollAutomation();
	__dpmi_yield();
}

/*--------------------------------------------------------------*/

bool
Timer_SetTimer(enum TimerType timer, bool set)
{
	assert(timer <= TIMER_GAME);

	if (set) {
		if (timer == TIMER_GAME) {
			if (enhancement_true_game_speed_adjustment) {
				TimerDOS_Install(timer, s_game_speed_hz[g_gameConfig.gameSpeed]);
			} else {
				TimerDOS_Install(timer, s_game_speed_hz[GAMESPEED_NORMAL]);
			}
		}

		s_started[timer] = true;
		return true;
	} else {
		if (g_host_type == HOSTTYPE_NONE)
			s_started[timer] = false;
		return false;
	}
}

int64_t
Timer_GetTimer(enum TimerType timer)
{
	assert(timer <= TIMER_GAME);

	/* Fold the 32-bit ISR counter into a 64-bit total. */
	s_count_base[timer] += TimerDOS_Take(&s_count[timer]);
	return s_count_base[timer];
}

bool
Timer_IsStarted(enum TimerType timer)
{
	assert(timer <= TIMER_GAME);

	return s_started[timer];
}

void
Timer_RegisterSource(void)
{
	TimerDOS_Take(&s_queued[TIMER_GUI]);
	TimerDOS_Take(&s_queued[TIMER_GAME]);
	s_queue_registered = true;
}

void
Timer_UnregisterSource(void)
{
	s_queue_registered = false;
}

static bool
TimerDOS_TakeQueued(enum TimerType timer)
{
	bool taken = false;

	disable();
	if (s_queued[timer] > 0) {
		s_queued[timer]--;
		taken = true;
	}
	enable();

	return taken;
}

enum TimerType
Timer_WaitForEvent(void)
{
	assert(s_queue_registered);

	for (;;) {
		if (TimerDOS_TakeQueued(TIMER_GUI))
			return TIMER_GUI;

		if (TimerDOS_TakeQueued(TIMER_GAME))
			return TIMER_GAME;

		TimerDOS_Idle();
	}
}

void
Timer_Sleep(int tics)
{
	TimerDOS_Take(&s_queued[TIMER_GUI]);

	for (int i = 0; i < tics; i++) {
		while (!TimerDOS_TakeQueued(TIMER_GUI))
			TimerDOS_Idle();

		Audio_PollMusic();
	}

	/* Game ticks during the sleep are not events for the caller. */
	if (!s_queue_registered)
		TimerDOS_Take(&s_queued[TIMER_GAME]);
}

bool
Timer_QueueIsEmpty(void)
{
	return (s_queued[TIMER_GUI] == 0) && (s_queued[TIMER_GAME] == 0);
}
