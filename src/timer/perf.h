#ifndef TIMER_PERF_H
#define TIMER_PERF_H

/* Section timing for DD_PROFILE in shared game code; no-ops except on
 * MS-DOS, and cheap there unless profiling. */
#ifdef __DJGPP__
#include "perf_dos.h"
#define PERF_SECTION_BEGIN(name) \
	const uint64_t perf_t0_##name = g_perf_detail ? Perf_Now() : 0
#define PERF_SECTION_END(name, counter) \
	do { if (g_perf_detail) Perf_Add(counter, (uint32_t)(Perf_Now() - perf_t0_##name)); } while (0)
#else
#define PERF_SECTION_BEGIN(name)
#define PERF_SECTION_END(name, counter)
#endif

#endif
