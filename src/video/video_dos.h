#ifndef VIDEO_VIDEODOS_H
#define VIDEO_VIDEODOS_H

#include <allegro.h>
#include "../gfx.h"

/* The 8bpp back buffer every draw call renders into. */
extern BITMAP *g_dos_backbuffer;

extern void VideoDOS_SetTransform(enum ScreenDivID div);
extern void VideoDOS_SetDivBitmap(enum ScreenDivID div, int x, int y, int w, int h);
extern void VideoDOS_ToScreen(float x, float y, int *sx, int *sy);
extern void VideoDOS_ScaleToScreen(float w, float h, int *sw, int *sh);
extern int VideoDOS_NearestColour(unsigned char r, unsigned char g, unsigned char b);
extern void VideoDOS_PollAutomation(void);

/* Performance summaries in DUNEDYN.LOG. */
extern void VideoDOS_MissionStart(void);
extern void VideoDOS_MissionEnd(void);
extern void VideoDOS_LogPerformance(const char *title);

/* audio_dos.cpp: sample voices playing, of those available. */
extern void AudioDOS_CountVoices(int *playing, int *total);

#endif
