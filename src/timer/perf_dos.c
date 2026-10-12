/* perf_dos.c
 *
 * Timing for the performance overlay, run log and DD_PROFILE.
 *
 * The clock is PIT channel 2 (the PC speaker timer), free running at
 * 1.193 MHz with the speaker disconnected.  Allegro owns channel 0, so
 * this needs no Allegro changes.  The 16-bit counter wraps every 55 ms;
 * the 60 Hz timer interrupt calls Perf_Now() to extend it to 64 bits.
 *
 * Each reading costs three port accesses (a few microseconds on ISA),
 * so time sections, not individual pixels.
 */

#include <allegro.h>
#include <dos.h>
#include <pc.h>
#include <stdio.h>
#include <string.h>
#include "perf_dos.h"

enum {
	PIT_CHANNEL2   = 0x42,
	PIT_COMMAND    = 0x43,
	SPEAKER_PORT   = 0x61,

	SPEAKER_GATE2  = 0x01,          /* Channel 2 counts while set. */
	SPEAKER_DATA   = 0x02,          /* Channel 2 drives the speaker while set. */
};

/* Written with interrupts disabled. */
static volatile uint64_t s_now;
static volatile uint16_t s_last_count;
static volatile uint64_t s_ticks[PERF_COUNTERS];

static bool s_initialised;
bool g_perf_detail;
static uint8_t s_old_speaker;

/* Main program only. */
static uint32_t s_frames;
static uint32_t s_game_ticks;
static uint32_t s_present_bytes;
static uint64_t s_last_frame;
static uint64_t s_worst_frame[PERF_WINDOWS];

static char s_cpu_name[64] = "unknown CPU";
static int s_cpu_mhz;
static bool s_cpu_mhz_exact;

/* Allegro's LOCK_FUNCTION markers. */
void Perf_Now_end(void);
void Perf_Add_end(void);

uint64_t
Perf_Now(void)
{
	if (!s_initialised)
		return 0;

	const int enabled = disable();

	/* Latch channel 2 and read it, low byte first.  It counts down. */
	outportb(PIT_COMMAND, 0x80);
	const uint8_t lo = inportb(PIT_CHANNEL2);
	const uint8_t hi = inportb(PIT_CHANNEL2);
	const uint16_t count = (uint16_t)(lo | (hi << 8));

	s_now += (uint16_t)(s_last_count - count);
	s_last_count = count;
	const uint64_t now = s_now;

	if (enabled)
		enable();

	return now;
}
END_OF_FUNCTION(Perf_Now)

/* Each counter is added to either by the main program or by our
 * interrupts, never both, and Perf_Snapshot() reads with interrupts
 * disabled, so no locking is needed here. */
void
Perf_Add(enum PerfCounter counter, uint32_t ticks)
{
	s_ticks[counter] += ticks;
}
END_OF_FUNCTION(Perf_Add)

double
Perf_TicksToMs(uint64_t ticks)
{
	return ticks * 1000.0 / PERF_TICKS_PER_SECOND;
}

/*--------------------------------------------------------------*/

static bool
Perf_HaveCPUID(void)
{
	uint32_t before, after;

	/* CPUID exists if the ID flag (bit 21) can be changed. */
	__asm__ __volatile__(
		"pushfl\n\t"
		"popl %0\n\t"
		"movl %0, %1\n\t"
		"xorl $0x200000, %1\n\t"
		"pushl %1\n\t"
		"popfl\n\t"
		"pushfl\n\t"
		"popl %1\n\t"
		"pushl %0\n\t"
		"popfl"
		: "=&r" (before), "=&r" (after));

	return ((before ^ after) & 0x200000) != 0;
}

static void
Perf_CPUID(uint32_t leaf, uint32_t r[4])
{
	__asm__ __volatile__("cpuid"
		: "=a" (r[0]), "=b" (r[1]), "=c" (r[2]), "=d" (r[3])
		: "a" (leaf), "c" (0));
}

static uint64_t
Perf_RDTSC(void)
{
	uint32_t lo, hi;
	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32) | lo;
}

static const char *
Perf_CPUModelName(const char *vendor, int family, int model)
{
	if (strcmp(vendor, "GenuineIntel") == 0) {
		if (family == 4) {
			switch (model) {
				case 0: case 1: return "Intel 486DX";
				case 2: return "Intel 486SX";
				case 3: return "Intel 486DX2";
				case 4: return "Intel 486SL";
				case 5: return "Intel 486SX2";
				case 7: return "Intel 486DX2 WB";
				case 8: return "Intel 486DX4";
				case 9: return "Intel 486DX4 WB";
			}
			return "Intel 486";
		}
		if (family == 5)
			return (model >= 4) ? "Intel Pentium MMX" : "Intel Pentium";
		if (family == 6) {
			switch (model) {
				case 1: return "Intel Pentium Pro";
				case 3: case 5: return "Intel Pentium II";
				case 6: return "Intel Celeron";
				case 7: case 8: case 10: case 11: return "Intel Pentium III";
			}
		}
	} else if (strcmp(vendor, "AuthenticAMD") == 0) {
		if (family == 4) {
			switch (model) {
				case 3: case 7: return "AMD 486DX2";
				case 8: case 9: return "AMD 486DX4";
				case 14: case 15: return "AMD Am5x86";
			}
			return "AMD 486";
		}
		if (family == 5) {
			if (model <= 3) return "AMD K5";
			if (model <= 7) return "AMD K6";
			if (model == 8) return "AMD K6-2";
			if (model == 9) return "AMD K6-III";
		}
	} else if (strcmp(vendor, "CyrixInstead") == 0) {
		if (family == 4) return "Cyrix 486/5x86";
		if (family == 5) return "Cyrix 6x86";
		if (family == 6) return "Cyrix 6x86MX";
	} else if (strcmp(vendor, "CentaurHauls") == 0) {
		return "IDT WinChip";
	}

	return NULL;
}

/* Time n iterations of a two-instruction loop, in PIT ticks. */
static uint32_t
Perf_TimeLoop(uint32_t n)
{
	const uint64_t t0 = Perf_Now();
	__asm__ __volatile__(
		".p2align 4\n"
		"1:\n\t"
		"decl %0\n\t"
		"jnz 1b"
		: "+r" (n));
	return (uint32_t)(Perf_Now() - t0);
}

static void
Perf_MeasureMHz(bool have_tsc)
{
	if (have_tsc) {
		uint64_t best = 0;

		/* Three 20 ms samples; interrupts only make a sample slower. */
		for (int i = 0; i < 3; i++) {
			const uint64_t t0 = Perf_Now();
			const uint64_t c0 = Perf_RDTSC();
			uint64_t t1;

			do {
				t1 = Perf_Now();
			} while (t1 - t0 < PERF_TICKS_PER_SECOND / 50);

			const uint64_t c1 = Perf_RDTSC();
			const uint64_t hz = (c1 - c0) * PERF_TICKS_PER_SECOND / (t1 - t0);
			if (hz > best)
				best = hz;
		}

		s_cpu_mhz = (int)((best + 500000) / 1000000);
		s_cpu_mhz_exact = true;
	} else {
		/* 486 family: dec + taken jnz is 1 + 3 clocks. */
		const uint32_t n = 200000;
		uint32_t best = 0xFFFFFFFF;

		Perf_TimeLoop(n / 10);
		for (int i = 0; i < 3; i++) {
			const uint32_t t = Perf_TimeLoop(n);
			if (t > 0 && t < best)
				best = t;
		}

		if (best != 0xFFFFFFFF)
			s_cpu_mhz = (int)(4.0 * n * PERF_TICKS_PER_SECOND / best / 1e6 + 0.5);
	}
}

static void
Perf_DetectCPU(void)
{
	bool have_tsc = false;

	if (!Perf_HaveCPUID()) {
		snprintf(s_cpu_name, sizeof(s_cpu_name), "486 (no CPUID)");
	} else {
		uint32_t r[4];
		char vendor[13];

		Perf_CPUID(0, r);
		const uint32_t max_leaf = r[0];
		memcpy(vendor + 0, &r[1], 4);
		memcpy(vendor + 4, &r[3], 4);
		memcpy(vendor + 8, &r[2], 4);
		vendor[12] = '\0';

		int family = 4, model = 0, stepping = 0;
		if (max_leaf >= 1) {
			Perf_CPUID(1, r);
			family = (r[0] >> 8) & 0xF;
			model = (r[0] >> 4) & 0xF;
			stepping = r[0] & 0xF;
			have_tsc = (r[3] & 0x10) != 0;
		}

		/* Later CPUs name themselves. */
		char brand[49] = "";
		Perf_CPUID(0x80000000, r);
		if (family >= 6 && r[0] >= 0x80000004 && r[0] < 0x80000100) {
			for (uint32_t i = 0; i < 3; i++) {
				Perf_CPUID(0x80000002 + i, r);
				memcpy(brand + 16 * i, r, 16);
			}
			brand[48] = '\0';
		}

		const char *s = brand;
		while (*s == ' ')
			s++;

		const char *name = Perf_CPUModelName(vendor, family, model);
		if (*s != '\0') {
			snprintf(s_cpu_name, sizeof(s_cpu_name), "%s", s);
		} else if (name != NULL) {
			snprintf(s_cpu_name, sizeof(s_cpu_name), "%s", name);
		} else {
			snprintf(s_cpu_name, sizeof(s_cpu_name), "%s family %d model %d stepping %d",
				vendor, family, model, stepping);
		}
	}

	Perf_MeasureMHz(have_tsc);
}

void
Perf_Init(void)
{
	if (s_initialised)
		return;

	LOCK_FUNCTION(Perf_Now);
	LOCK_FUNCTION(Perf_Add);
	LOCK_VARIABLE(s_now);
	LOCK_VARIABLE(s_last_count);
	LOCK_VARIABLE(s_ticks);
	LOCK_VARIABLE(s_initialised);

	const int enabled = disable();

	/* Gate channel 2 on, speaker off; mode 2 (rate generator), binary,
	 * reload 0 = 65536. */
	s_old_speaker = inportb(SPEAKER_PORT);
	outportb(SPEAKER_PORT, (s_old_speaker | SPEAKER_GATE2) & ~SPEAKER_DATA);
	outportb(PIT_COMMAND, 0xB4);
	outportb(PIT_CHANNEL2, 0);
	outportb(PIT_CHANNEL2, 0);
	s_last_count = 0;
	s_now = 0;
	s_initialised = true;

	if (enabled)
		enable();

	Perf_Now();
	Perf_DetectCPU();
	s_last_frame = Perf_Now();
}

void
Perf_Uninit(void)
{
	if (!s_initialised)
		return;

	const int enabled = disable();
	s_initialised = false;
	outportb(SPEAKER_PORT, s_old_speaker & ~SPEAKER_DATA);
	if (enabled)
		enable();
}

void
Perf_AddFrame(uint32_t present_bytes)
{
	const uint64_t now = Perf_Now();
	const uint64_t frame = now - s_last_frame;

	s_last_frame = now;
	s_frames++;
	s_present_bytes += present_bytes;

	for (int i = 0; i < PERF_WINDOWS; i++) {
		if (frame > s_worst_frame[i])
			s_worst_frame[i] = frame;
	}
}

/* Restart frame timing, e.g. after loading, so the wait is not counted
 * as a slow frame. */
void
Perf_ResetFrameTime(void)
{
	s_last_frame = Perf_Now();
}

void
Perf_AddGameTick(void)
{
	s_game_ticks++;
}

void
Perf_Snapshot(PerfSnapshot *snap, enum PerfWindow window)
{
	snap->time = Perf_Now();
	snap->frames = s_frames;
	snap->game_ticks = s_game_ticks;
	snap->present_bytes = s_present_bytes;
	snap->worst_frame = s_worst_frame[window];
	s_worst_frame[window] = 0;

	const int enabled = disable();
	memcpy(snap->ticks, (const void *)s_ticks, sizeof(snap->ticks));
	if (enabled)
		enable();
}

const char *
Perf_CPUName(void)
{
	return s_cpu_name;
}

int
Perf_CPUMHz(void)
{
	return s_cpu_mhz;
}

bool
Perf_CPUMHzExact(void)
{
	return s_cpu_mhz_exact;
}
