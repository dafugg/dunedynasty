/** @file src/dos/config_dos.c
 *
 * MS-DOS command line overrides for one run, kept out of the saved
 * configuration (called from config_a5.c).
 */

#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "../os/math.h"

#include "config_dos.h"

#include "../audio/audio.h"
#include "../common_dos.h"
#include "../config.h"
#include "../gfx.h"

/* Command line overrides (-res, -scale, -voices, -quality, -allaudio)
 * apply to this run only: the saved values are swapped back in while
 * saving. */
static struct {
	bool active;
	DisplayMode displayMode;
	float scale[3];
	int voices;
	int quality;
	bool music;
	enum SoundEffectSources effects;
	bool speech;
} s_saved_options;

static const enum ScreenDivID s_scaled_divs[3] = {
	SCREENDIV_MENUBAR, SCREENDIV_SIDEBAR, SCREENDIV_VIEWPORT
};

void
ConfigDOS_SwapCommandLineOptions(void)
{
	if (!s_saved_options.active)
		return;

	DisplayMode mode = g_gameConfig.displayMode;
	g_gameConfig.displayMode = s_saved_options.displayMode;
	s_saved_options.displayMode = mode;

	for (int i = 0; i < 3; i++) {
		const float scale = g_screenDiv[s_scaled_divs[i]].scalex;
		g_screenDiv[s_scaled_divs[i]].scalex = s_saved_options.scale[i];
		s_saved_options.scale[i] = scale;
	}

	int v = g_dos_sound_voices;
	g_dos_sound_voices = s_saved_options.voices;
	s_saved_options.voices = v;

	v = g_dos_mixer_quality;
	g_dos_mixer_quality = s_saved_options.quality;
	s_saved_options.quality = v;

	bool b = g_enable_music;
	g_enable_music = s_saved_options.music;
	s_saved_options.music = b;

	const enum SoundEffectSources e = g_enable_sound_effects;
	g_enable_sound_effects = s_saved_options.effects;
	s_saved_options.effects = e;

	b = g_enable_voices;
	g_enable_voices = s_saved_options.speech;
	s_saved_options.speech = b;
}

void
ConfigDOS_ApplyCommandLine(void)
{
	s_saved_options.displayMode = g_gameConfig.displayMode;
	for (int i = 0; i < 3; i++)
		s_saved_options.scale[i] = g_screenDiv[s_scaled_divs[i]].scalex;
	s_saved_options.voices = g_dos_sound_voices;
	s_saved_options.quality = g_dos_mixer_quality;
	s_saved_options.music = g_enable_music;
	s_saved_options.effects = g_enable_sound_effects;
	s_saved_options.speech = g_enable_voices;
	s_saved_options.active = true;

	/* -allaudio: music, synth and digital effects, and speech. */
	if (DOS_HasArg("allaudio")) {
		g_enable_music = true;
		g_enable_sound_effects = SOUNDEFFECTS_SYNTH_AND_SAMPLES;
		g_enable_voices = true;
	}

	const char *arg = DOS_ArgValue("res");
	int w, h;
	if (arg != NULL && sscanf(arg, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
		g_gameConfig.displayMode.width = w;
		g_gameConfig.displayMode.height = h;
	}

	arg = DOS_ArgValue("scale");
	if (arg != NULL) {
		const float scale = clamp(1.0f, (float)atof(arg), 8.0f);
		for (int i = 0; i < 3; i++)
			g_screenDiv[s_scaled_divs[i]].scalex = scale;
	}

	arg = DOS_ArgValue("voices");
	if (arg != NULL)
		g_dos_sound_voices = clamp(1, atoi(arg), 16);

	arg = DOS_ArgValue("quality");
	if (arg != NULL)
		g_dos_mixer_quality = clamp(0, atoi(arg), 3);

	TRUE_DISPLAY_WIDTH = g_gameConfig.displayMode.width;
	TRUE_DISPLAY_HEIGHT = g_gameConfig.displayMode.height;
}
