/* common_dos.c
 *
 * MS-DOS platform start-up and screen division layout (Allegro 4).
 * Provides the same A5_* entry points as common_a5.c so that the
 * game code is shared between platforms.
 */

#include <allegro.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "errorlog.h"
#include "os/math.h"

#include "common_a5.h"
#include "common_dos.h"
#include "audio/audio_a5.h"
#include "config.h"
#include "crashlog/runlog_dos.h"
#include "input/input_a5.h"
#include "input/mouse.h"
#include "timer/perf_dos.h"
#include "timer/timer_dos.h"
#include "video/video_a5.h"
#include "video/video_dos.h"

static enum ScreenDivID curr_transform;

extern int __crt0_argc;
extern char **__crt0_argv;

static int
DOS_FindArg(const char *name)
{
	for (int i = 1; i < __crt0_argc; i++) {
		const char *arg = __crt0_argv[i];

		/* Accept -name and /name, any case. */
		if ((arg[0] == '-' || arg[0] == '/') && strcasecmp(arg + 1, name) == 0)
			return i;
	}

	return -1;
}

bool
DOS_HasArg(const char *name)
{
	return DOS_FindArg(name) >= 0;
}

/* The word after a switch: "-voices 4" gives "4". */
const char *
DOS_ArgValue(const char *name)
{
	const int i = DOS_FindArg(name);
	return (i >= 0 && i + 1 < __crt0_argc) ? __crt0_argv[i + 1] : NULL;
}

/* Lock all memory: timer interrupts run the AdLib driver (C++) and
 * Allegro's mixer, which must never be paged out by the DPMI host. */
#include <crt0.h>
int _crt0_startup_flags = _CRT0_FLAG_LOCK_MEMORY;

/*--------------------------------------------------------------*/

static void
DOS_InitScreenDiv(enum ScreenDivID divID, float x1, float y1, float x2, float y2)
{
	ScreenDiv *div = &g_screenDiv[divID];

	if (g_aspect_correction == ASPECT_RATIO_CORRECTION_FULL) {
		div->scaley = div->scalex * GFX_AspectCorrection_GetRatio();
	} else {
		div->scaley = div->scalex;
	}

	if (divID == SCREENDIV_MENUBAR) {
		y2 = div->scaley * y2;
	}

	const float w = x2 - x1;
	const float h = y2 - y1;

	div->x = x1;
	div->y = y1;
	div->width = ceil(w / div->scalex);
	div->height = ceil(h / div->scaley);

	/* Allegro 5 draws these through a sub-bitmap, shaken with the screen. */
	VideoDOS_SetDivBitmap(divID, x1, y1 + GFX_ScreenShake_Offset(), w, h);
}

/**
 * Same layout as A5_InitTransform, without the Allegro 5 sub-bitmaps.
 * The software renderer reads g_screenDiv directly.
 */
void
A5_InitTransform(bool screen_size_changed)
{
	VARIABLE_NOT_USED(screen_size_changed);

	const float pixel_aspect_ratio = GFX_AspectCorrection_GetRatio();

	const double preferred_aspect = (double)SCREEN_WIDTH / SCREEN_HEIGHT;
	const double aspect = pixel_aspect_ratio * TRUE_DISPLAY_WIDTH / TRUE_DISPLAY_HEIGHT;
	float scale = (double)TRUE_DISPLAY_WIDTH / SCREEN_WIDTH;
	float offx = 0.0f;
	float offy = 0.0f;

	/* Tall screens. */
	if (aspect < preferred_aspect - 0.001) {
		const double newh = pixel_aspect_ratio * scale * SCREEN_HEIGHT;
		offy = (TRUE_DISPLAY_HEIGHT - newh) / 2;
	} else if (aspect > preferred_aspect + 0.001) {
		scale = (double)TRUE_DISPLAY_HEIGHT / (pixel_aspect_ratio * SCREEN_HEIGHT);
		const double neww = scale * SCREEN_WIDTH;
		offx = (TRUE_DISPLAY_WIDTH - neww) / 2;
	}

	/* Main, menu and hud use full screen sub-bitmaps. */
	VideoDOS_SetDivBitmap(SCREENDIV_MAIN, 0, 0, TRUE_DISPLAY_WIDTH, TRUE_DISPLAY_HEIGHT);
	VideoDOS_SetDivBitmap(SCREENDIV_MENU, 0, 0, TRUE_DISPLAY_WIDTH, TRUE_DISPLAY_HEIGHT);
	VideoDOS_SetDivBitmap(SCREENDIV_HUD, 0, 0, TRUE_DISPLAY_WIDTH, TRUE_DISPLAY_HEIGHT);

	/* Menu. */
	{
		ScreenDiv *div = &g_screenDiv[SCREENDIV_MENU];

		div->x = offx;
		div->y = offy;
		div->scalex = scale;
		div->scaley = scale * pixel_aspect_ratio;
	}

	/* Hud. */
	{
		ScreenDiv *div = &g_screenDiv[SCREENDIV_HUD];

		div->scalex = scale / 2;
		div->scaley = (scale / 2) * pixel_aspect_ratio;
		div->width  = TRUE_DISPLAY_WIDTH / div->scalex;
		div->height = TRUE_DISPLAY_HEIGHT / div->scaley;
		div->x = 0;
		div->y = 0;
	}

	ScreenDiv *menubar = &g_screenDiv[SCREENDIV_MENUBAR];
	DOS_InitScreenDiv(SCREENDIV_MENUBAR,
			0.0f, 0.0f, TRUE_DISPLAY_WIDTH, 40.0f);

	ScreenDiv *sidebar = &g_screenDiv[SCREENDIV_SIDEBAR];
	DOS_InitScreenDiv(SCREENDIV_SIDEBAR,
			TRUE_DISPLAY_WIDTH - sidebar->scalex * 80.0f, menubar->scaley * menubar->height,
			TRUE_DISPLAY_WIDTH, TRUE_DISPLAY_HEIGHT);

	DOS_InitScreenDiv(SCREENDIV_VIEWPORT,
			0.0f, menubar->scaley * menubar->height, sidebar->x, TRUE_DISPLAY_HEIGHT);

	A5_UseTransform(curr_transform);
}

bool
A5_InitSystem(void)
{
	/* Keep the log intact if we crash or are killed. */
	setvbuf(stdout, NULL, _IONBF, 0);

	RunLog_Open(__crt0_argv[0]);
	Perf_Init();

	if (allegro_init() != 0) {
		Error("allegro_init() failed.\n");
		return false;
	}

	DOS_LogSystemReport();

	ConfigA5_InitDataDirectoriesAndLoadConfigFile();
	return true;
}

bool
A5_Init(void)
{
	GameOptions_Load();

	if (!TimerDOS_Init())
		return false;

	if (!VideoA5_Init())
		return false;

	/* After the video mode is set, so the mouse range matches the screen. */
	if (!InputA5_Init())
		return false;

	AudioA5_Init();

	A5_InitTransform(true);

	DOS_LogSettings();
	return true;
}

void
A5_Uninit(void)
{
	VideoDOS_LogPerformance("Whole run");
	GameOptions_Save();

	AudioA5_Uninit();
	InputA5_Uninit();
	VideoA5_Uninit();
	TimerDOS_Uninit();

	RunLog_Printf("Exit.\n");
	Perf_Uninit();
	RunLog_Close();
}

void
A5_UseTransform(enum ScreenDivID div)
{
	VideoDOS_SetTransform(div);
	curr_transform = div;
}

enum ScreenDivID
A5_SaveTransform(void)
{
	return curr_transform;
}
