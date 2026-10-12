#ifndef TIMER_PERFDOS_H
#define TIMER_PERFDOS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Perf_Now() counts PIT input clocks. */
#define PERF_TICKS_PER_SECOND   1193182u

enum PerfCounter {
	/* Main program. */
	PERF_GUI_TICKS,         /* Input and interface logic (60 Hz). */
	PERF_GAME_TICKS,        /* Game logic and AI. */
	PERF_DRAW,              /* Drawing the interface and viewport. */
	PERF_BLIT,              /* ... of which in the 8bpp blitter. */
	PERF_CLEAR,             /* Clearing the back buffer. */
	PERF_PRESENT,           /* Copying to video memory. */

	/* Our interrupts. */
	PERF_IRQ_MUSIC,         /* AdLib and MIDI music drivers. */

	/* Parts of PERF_DRAW, timed only for DD_PROFILE (see perf.h). */
	PERF_DRAW_MENUBAR,
	PERF_DRAW_SIDEBAR,
	PERF_DRAW_FOGUPDATE,    /* Map_Client_UpdateFogOfWar. */
	PERF_DRAW_TILES,
	PERF_DRAW_UNITS,
	PERF_DRAW_EXPLOSIONS,
	PERF_DRAW_FOG,          /* Shroud and fog edges. */
	PERF_DRAW_MARKERS,      /* Rally points, health bars, selection. */
	PERF_DRAW_AIR,
	PERF_DRAW_UNITMAP,      /* Unit_UpdateMap after drawing. */
	PERF_DRAW_MINIMAP,      /* Part of PERF_DRAW_MENUBAR. */
	PERF_DRAW_VIEWPORT,     /* Whole viewport: tiles .. air units. */
	PERF_DRAW_CHAT,
	PERF_DRAW_SCREEN,       /* GUI_DrawScreen: fog update + viewport. */
	PERF_DRAW_INTERFACE,    /* GUI_DrawInterfaceAndRadar, everything. */

	PERF_COUNTERS
};

/* Who is asking for the worst frame time; each is reset on its own. */
enum PerfWindow {
	PERF_WINDOW_OVERLAY,
	PERF_WINDOW_MISSION,
	PERF_WINDOW_PROFILE,
	PERF_WINDOW_RUN,

	PERF_WINDOWS
};

typedef struct PerfSnapshot {
	uint64_t time;                      /* Perf_Now() when taken. */
	uint32_t frames;                    /* Frames presented. */
	uint32_t game_ticks;                /* Game logic ticks run. */
	uint32_t present_bytes;             /* Bytes sent to video memory. */
	uint64_t worst_frame;               /* Longest frame since the window's last snapshot. */
	uint64_t ticks[PERF_COUNTERS];
} PerfSnapshot;

extern void Perf_Init(void);
extern void Perf_Uninit(void);

/* Monotonic time in PIT clocks, safe in interrupts.  Must be called at
 * least every 50 ms; the 60 Hz timer interrupt does. */
extern uint64_t Perf_Now(void);

extern void Perf_Add(enum PerfCounter counter, uint32_t ticks);
extern void Perf_AddFrame(uint32_t present_bytes);
extern void Perf_ResetFrameTime(void);
extern void Perf_AddGameTick(void);
extern void Perf_Snapshot(PerfSnapshot *snap, enum PerfWindow window);

extern double Perf_TicksToMs(uint64_t ticks);

/* Fine-grained draw timing is on (DD_PROFILE). */
extern bool g_perf_detail;

/* The CPU found at start-up. */
extern const char *Perf_CPUName(void);
extern int Perf_CPUMHz(void);           /* 0 if unknown. */
extern bool Perf_CPUMHzExact(void);     /* Measured with a time stamp counter, not estimated. */

#ifdef __cplusplus
}
#endif

#endif
