/* sysinfo_dos.c
 *
 * The start-up report at the top of DUNEDYN.LOG: what the machine is,
 * what was detected, and the settings in effect.  Enough to answer
 * "why is it slow / silent / black on my machine" from the log alone.
 */

#include <allegro.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <libc/malldbg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>
#include "buildcfg.h"
#include "types.h"

#include "common_dos.h"
#include "audio/audio.h"
#include "config.h"
#include "gfx.h"
#include "crashlog/runlog_dos.h"
#include "table/sound.h"
#include "timer/perf_dos.h"

extern int __crt0_argc;
extern char **__crt0_argv;

/* DOS_LogMemory: the least physical memory seen free, and the amount
 * in use at the last report. */
static unsigned long s_mem_least_free_kb = ~0ul;
static unsigned long s_mem_last_used_kb;

static void
SysInfo_LogCommandLine(void)
{
	char line[256] = "";

	for (int i = 1; i < __crt0_argc; i++) {
		strncat(line, " ", sizeof(line) - strlen(line) - 1);
		strncat(line, __crt0_argv[i], sizeof(line) - strlen(line) - 1);
	}

	RunLog_Printf("Command line: %s%s\n", __crt0_argv[0], line);
}

static void
SysInfo_LogOS(void)
{
	const int dos = _get_dos_version(1);
	RunLog_Printf("DOS: %s %d.%02d\n", _os_flavor, dos >> 8, dos & 0xFF);

	if (os_type != OSTYPE_UNKNOWN) {
		RunLog_Printf("Running under: %c%c%c%c %d.%d (expect slower timing and sound)\n",
			(os_type >> 24) & 0xFF, (os_type >> 16) & 0xFF, (os_type >> 8) & 0xFF, os_type & 0xFF,
			os_version, os_revision);
	}

	__dpmi_version_ret ver;
	if (__dpmi_get_version(&ver) == 0) {
		char host[160] = "";
		int flags;
		unsigned char caps[128];

		/* Host name is a DPMI 1.0 feature; CWSDPMI 0.90+ answers it. */
		if (__dpmi_get_capabilities(&flags, (char *)caps) == 0) {
			caps[sizeof(caps) - 1] = '\0';
			snprintf(host, sizeof(host), " (%s)", caps + 2);
		}

		RunLog_Printf("DPMI: version %d.%02d%s, %s mode\n", ver.major, ver.minor, host,
			(ver.flags & 2) ? "real" : "V86");
	}
}

static void
SysInfo_LogMemory(void)
{
	_go32_dpmi_meminfo info;

	if (_go32_dpmi_get_free_memory_information(&info) != 0)
		return;

	const unsigned long total_kb = (unsigned long)info.total_physical_pages * 4;
	s_mem_last_used_kb = total_kb - (unsigned long)info.available_physical_pages * 4;
	RunLog_Printf("Memory: %lu KB physical, %lu KB free%s\n", total_kb,
		(unsigned long)info.available_physical_pages * 4,
		(total_kb < 12 * 1024) ? " (16 MB needed)" : "");
}

/*--------------------------------------------------------------*/
/* Memory use: with all memory locked (common_dos.c), physical memory
 * in use is what the program has ever taken; DJGPP's heap does not give
 * memory back, so the most ever in use is what decides whether a
 * machine has enough. */

static bool
SysInfo_MemoryKB(unsigned long *total_kb, unsigned long *free_kb)
{
	_go32_dpmi_meminfo info;

	if (_go32_dpmi_get_free_memory_information(&info) != 0)
		return false;

	*total_kb = (unsigned long)info.total_physical_pages * 4;
	*free_kb = (unsigned long)info.available_physical_pages * 4;
	if (*free_kb < s_mem_least_free_kb)
		s_mem_least_free_kb = *free_kb;
	return true;
}

/* Debugging: built with -DDD_MEMTRACE and linked with
 * -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc, every allocation of
 * 4 KB or more is logged with its caller's address (see DOS-PLAN.md). */
#ifdef DD_MEMTRACE
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
static int s_in_trace;
static void
SysInfo_Trace(const char *what, size_t n, void *caller)
{
	if (n >= 4096 && !s_in_trace) {
		s_in_trace = 1;
		RunLog_Printf("MEMTRACE %s %lu %p\n", what, (unsigned long)n, caller);
		s_in_trace = 0;
	}
}
void *__wrap_malloc(size_t n) { SysInfo_Trace("m", n, __builtin_return_address(0)); return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t m) { SysInfo_Trace("c", n * m, __builtin_return_address(0)); return __real_calloc(n, m); }
void *__wrap_realloc(void *p, size_t n) { SysInfo_Trace("r", n, __builtin_return_address(0)); return __real_realloc(p, n); }
#endif

/* Called now and then while playing, to catch the peak. */
void
DOS_SampleMemory(void)
{
	unsigned long total_kb, free_kb;
	SysInfo_MemoryKB(&total_kb, &free_kb);
}

void
DOS_LogMemory(const char *when)
{
	unsigned long total_kb, free_kb;

	if (!SysInfo_MemoryKB(&total_kb, &free_kb))
		return;

	const struct mallinfo mi = mallinfo();
	const unsigned long used_kb = total_kb - free_kb;

	RunLog_Printf("Memory %s: %lu KB used (%+ld), most %lu KB; heap %d KB in use of %d KB\n",
		when, used_kb, (long)used_kb - (long)s_mem_last_used_kb,
		total_kb - s_mem_least_free_kb, mi.uordblks / 1024, mi.arena / 1024);
	s_mem_last_used_kb = used_kb;
}

/* Read an ASCIIZ string at a real-mode far pointer. */
static void
SysInfo_ReadFarString(uint32 farptr, char *buf, size_t size)
{
	const unsigned long addr = ((farptr >> 16) << 4) + (farptr & 0xFFFF);

	buf[0] = '\0';
	if (farptr == 0 || addr >= 0x110000)
		return;

	dosmemget(addr, size - 1, buf);
	buf[size - 1] = '\0';

	for (char *c = buf; *c != '\0'; c++) {
		if (*c == '\r' || *c == '\n') {
			*c = '\0';
			break;
		}
		if ((unsigned char)*c < ' ' || (unsigned char)*c >= 0x7F)
			*c = '?';
	}
}

static void
SysInfo_LogVESA(void)
{
	unsigned char info[512];
	__dpmi_regs r;

	memset(info, 0, sizeof(info));
	memcpy(info, "VBE2", 4);
	dosmemput(info, sizeof(info), __tb);

	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F00;
	r.x.es = __tb >> 4;
	r.x.di = __tb & 0x0F;
	__dpmi_int(0x10, &r);

	if (r.x.ax != 0x004F) {
		RunLog_Printf("VESA: no VESA BIOS (only 320x200 and 320x240 work; try UniVBE)\n");
		return;
	}

	dosmemget(__tb, sizeof(info), info);
	if (memcmp(info, "VESA", 4) != 0)
		return;

	const int version = info[4] | (info[5] << 8);
	const int memory_kb = (info[18] | (info[19] << 8)) * 64;
	char oem[80];

	SysInfo_ReadFarString(*(uint32 *)&info[6], oem, sizeof(oem));
	RunLog_Printf("VESA: version %d.%d, %d KB video memory, %s\n",
		version >> 8, version & 0xFF, memory_kb, oem);

	if (version >= 0x200) {
		char vendor[80], product[80], rev[80];

		SysInfo_ReadFarString(*(uint32 *)&info[22], vendor, sizeof(vendor));
		SysInfo_ReadFarString(*(uint32 *)&info[26], product, sizeof(product));
		SysInfo_ReadFarString(*(uint32 *)&info[30], rev, sizeof(rev));
		RunLog_Printf("VESA: %s %s %s\n", vendor, product, rev);
	} else {
		RunLog_Printf("VESA: version 1.x has no linear frame buffer; higher resolutions use slower banked copies\n");
	}
}

static void
SysInfo_LogMouse(void)
{
	__dpmi_raddr vec;
	__dpmi_regs r;

	if (__dpmi_get_real_mode_interrupt_vector(0x33, &vec) != 0 || (vec.segment == 0 && vec.offset16 == 0)) {
		RunLog_Printf("Mouse: no driver\n");
		return;
	}

	memset(&r, 0, sizeof(r));
	r.x.ax = 0x0000;
	__dpmi_int(0x33, &r);
	if (r.x.ax != 0xFFFF) {
		RunLog_Printf("Mouse: no driver\n");
		return;
	}
	const int buttons = r.x.bx;

	static const char *types[] = { "?", "bus", "serial", "InPort", "PS/2", "HP" };
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x0024;
	__dpmi_int(0x33, &r);

	if (r.x.ax != 0xFFFF && r.h.bh != 0) {
		/* PS/2 mice report IRQ 0 (it is always 12). */
		RunLog_Printf("Mouse: driver %d.%02x, %s, IRQ %d, %d buttons\n",
			r.h.bh, r.h.bl, (r.h.ch < 6) ? types[r.h.ch] : "?", (r.h.ch == 4) ? 12 : r.h.cl, buttons);
	} else {
		RunLog_Printf("Mouse: driver found, %d buttons\n", buttons);
	}
}

void
DOS_LogSystemReport(void)
{
	RunLog_Printf("%s %s for MS-DOS, built %s\n", DUNE_DYNASTY_STR, DUNE_DYNASTY_VERSION, __DATE__);
	SysInfo_LogCommandLine();

	if (Perf_CPUMHz() > 0) {
		RunLog_Printf("CPU: %s, %s%d MHz%s\n", Perf_CPUName(),
			Perf_CPUMHzExact() ? "" : "about ", Perf_CPUMHz(),
			(cpu_capabilities & CPU_FPU) ? "" : ", no FPU (very slow)");
	} else {
		RunLog_Printf("CPU: %s\n", Perf_CPUName());
	}

	SysInfo_LogOS();
	SysInfo_LogMemory();
	SysInfo_LogVESA();
	SysInfo_LogMouse();

	const char *blaster = getenv("BLASTER");
	RunLog_Printf("BLASTER=%s\n", (blaster != NULL) ? blaster : "(not set: no Sound Blaster sound)");
}

/* After start-up: the settings actually used, with overrides applied. */
void
DOS_LogSettings(void)
{
	RunLog_Printf("Settings: %dx%d, viewport scale %.0f, game speed %d, language %d\n",
		g_gameConfig.displayMode.width, g_gameConfig.displayMode.height,
		g_screenDiv[SCREENDIV_VIEWPORT].scalex, g_gameConfig.gameSpeed, g_gameConfig.language);
	RunLog_Printf("Settings: sound %s, music %s, effects %d, voices %s, music set %s\n",
		g_enable_audio ? "on" : "off", g_enable_music ? "on" : "off",
		g_enable_sound_effects, g_enable_voices ? "on" : "off",
		g_table_music_set[default_music_pack].name);
}
