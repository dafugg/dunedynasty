#ifndef TIMER_PERF_H
#define TIMER_PERF_H

/* Performance overlay and log hooks in shared game code (MS-DOS
 * F10 overlay, DUNEDYN.LOG summaries, DD_PROFILE).  No-ops except on
 * MS-DOS.
 *
 *   PERF_TIME_BEGIN(name) ... PERF_TIME_END(name, counter)
 *                       time a part of the main loop, always
 *   PERF_SECTION_BEGIN/END  the same, only while profiling (to find
 *                       where time goes; remove when done)
 *   PERF_GAME_TICK()    a game logic step was run
 *   PERF_MISSION_START() / PERF_MISSION_END()
 */
#ifdef __DJGPP__
#include "perf_dos.h"
#define PERF_TIME_BEGIN(name) \
	const uint64_t perf_t0_##name = Perf_Now()
#define PERF_TIME_END(name, counter) \
	Perf_Add(counter, (uint32_t)(Perf_Now() - perf_t0_##name))
#define PERF_SECTION_BEGIN(name) \
	const uint64_t perf_t0_##name = g_perf_detail ? Perf_Now() : 0
#define PERF_SECTION_END(name, counter) \
	do { if (g_perf_detail) Perf_Add(counter, (uint32_t)(Perf_Now() - perf_t0_##name)); } while (0)
#define PERF_GAME_TICK()        Perf_AddGameTick()
#define PERF_MISSION_START()    Perf_MissionStart()
#define PERF_MISSION_END()      Perf_MissionEnd()
#else
#define PERF_TIME_BEGIN(name)
#define PERF_TIME_END(name, counter)
#define PERF_SECTION_BEGIN(name)
#define PERF_SECTION_END(name, counter)
#define PERF_GAME_TICK()
#define PERF_MISSION_START()
#define PERF_MISSION_END()
#endif

#endif
