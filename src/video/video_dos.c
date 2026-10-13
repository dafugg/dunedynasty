/* video_dos.c
 *
 * MS-DOS 8bpp software renderer on Allegro 4 (VESA / VGA).
 *
 * Mirrors video_a5.c: the same sprites, icons, CPS images and fonts are
 * exported once from the 8bpp game data into "textures", which here
 * are 8bpp memory bitmaps instead of RGBA GPU textures.  Every draw
 * call goes through VideoDOS_Blit, which applies the current ScreenDiv
 * scale and offset and one of a few 8bpp blend modes:
 *
 *   masked/opaque copies, palette remaps (fonts), solid colour (white
 *   masks that Allegro 5 tints), shade tables (alpha black: shadows,
 *   fog of war, screen shading), and pixel shifts (sandworm / sonic
 *   blur, as in the original game).
 *
 * Unlike Allegro 5, colours are palette indices resolved at display
 * time, so palette animation (windtraps, fades) works as in Dune II.
 * Images exported with BENE.PAL switch the hardware palette for the
 * frame they are drawn in.
 */

#include <allegro.h>
#include <assert.h>
#include <dpmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errorlog.h"
#include "../common_dos.h"
#include "../crashlog/runlog_dos.h"
#include "../os/common.h"
#include "../os/math.h"

#include "terraincache_dos.h"
#include "uicache_dos.h"
#include "video_dos.h"
#include "video_a5.h"

#include "../common_a5.h"
#include "../config.h"
#include "../enhancement.h"
#include "../file.h"
#include "../gfx.h"
#include "../gui/font.h"
#include "../gui/gui.h"
#include "../input/input_a5.h"
#include "../input/mouse.h"
#include "../map.h"
#include "../opendune.h"
#include "../scenario.h"
#include "../sprites.h"
#include "../structure.h"
#include "../table/widgetinfo.h"
#include "../timer/perf_dos.h"
#include "../timer/timer.h"
#include "../timer/timer_dos.h"
#include "../tools/coord.h"
#include "../tools/random_xorshift.h"
#include "../wsa.h"

#define ICONID_MAX          512
#define SHAPEID_MAX         640
#define FONTID_MAX          8
#define CURSOR_MAX          6
#define SHADE_LEVELS        16
#define WSA_STATIC_MAX      21

enum BitmapCopyMode {
	TRANSPARENT_COLOUR_0,
	BLACK_COLOUR_0,
	SKIP_COLOUR_0
};

enum BlitMode {
	BLIT_OPAQUE,        /* dst = src */
	BLIT_MASKED,        /* dst = src, where src != 0 */
	BLIT_LUT,           /* dst = lut[src], where src != 0 */
	BLIT_SOLID,         /* dst = colour, where src != 0 */
	BLIT_SHADE,         /* dst = lut[dst], where src != 0 */
	BLIT_SHIFT          /* dst = dst[x + colour], where src != 0 */
};

typedef struct CPSStore {
	struct CPSStore *next;

	char filename[128];
	BITMAP *bmp;
	bool benepal;
} CPSStore;

typedef struct FadeInAux {
	bool fade_in;   /* false to fade out. */
	int frame;      /* 0 <= frame < height. */

	BITMAP *bmp;
	bool masked;    /* Shapes: colour 0 is transparent. */
	int x, y;
	int width;
	int height;

	/* Pixels revealed so far, width * height. */
	unsigned char *revealed;

	/* Persistent random data. */
	int cols[SCREEN_WIDTH];
	int rows[SCREEN_HEIGHT];
} FadeInAux;

typedef struct IconCoord {
	int sx, sy;
} IconCoord;

/* How an icon can be drawn, worked out once at load time. */
enum IconKind {
	ICON_MASKED,        /* Has transparent pixels. */
	ICON_OPAQUE,        /* No transparent pixels: plain copy. */
	ICON_SOLID          /* One colour everywhere: fill. */
};

typedef struct Src8 {
	const unsigned char *data;
	int pitch;
} Src8;

static const struct CPSSpecialCoord {
	int cx, cy; /* coordinates in cps. */
	int tx, ty; /* coordinates in texture. */
	int w, h;
} cps_special_coord[CPS_SPECIAL_MAX] = {
	{   0,   0,   2,   2, 184, 17 }, /* CPS_MENUBAR_LEFT */
	{  -1,  -1,   2,  22, 320, 17 }, /* CPS_MENUBAR_MIDDLE */
	{ 184,   0, 189,   2, 136, 17 }, /* CPS_MENUBAR_RIGHT */
	{   0,  17,   2,  42,   8, 23 }, /* CPS_STATUSBAR_LEFT */
	{  -1,  -1,  13,  42, 425, 23 }, /* CPS_STATUSBAR_MIDDLE */
	{ 312,  17, 441,  42,   8, 23 }, /* CPS_STATUSBAR_RIGHT */
	{ 240,  40,   2,  68,  80, 17 }, /* CPS_SIDEBAR_TOP */
	{ 240,  63,   2,  88,  16, 52 }, /* CPS_SIDEBAR_MIDDLE */
	{ 240, 115,   2, 162,  80, 85 }, /* CPS_SIDEBAR_BOTTOM */
	{   8,   0, 192,  68, 304, 24 }, /* CPS_CONQUEST_EN */
	{   8,  96, 192, 118, 304, 24 }, /* CPS_CONQUEST_FR */
	{   8, 120, 192, 168, 304, 24 }, /* CPS_CONQUEST_DE */
};

static const uint8 font_palette[][8] = {
	{ 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* No outline. */
	{ 0x00, 0xFF, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* Shadow. */
	{ 0x00, 0xFF, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00 }, /* Outline. */
	{ 0x00, 0xFF, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0x00 }, /* Intro. */
};

static const struct {
	int x, y;
} cursor_focus[CURSOR_MAX] = {
	{ 0, 0 }, { 5, 0 }, { 8, 5 }, { 5, 8 }, { 0, 5 }, { 8, 8 }
};

BITMAP *g_dos_backbuffer;
enum GraphicsDriver g_graphics_driver;

static enum ScreenDivID s_div = SCREENDIV_MAIN;

/* Each ScreenDiv draws through an Allegro 5 sub-bitmap: its origin and
 * size on screen, and its own clipping rectangle (relative to the
 * origin), which is restored whenever the div becomes current. */
static struct {
	int x, y, w, h;
	int cx1, cy1, cx2, cy2;     /* Clip, screen pixels, exclusive end. */
} s_div_bitmap[SCREENDIV_MAX];

/* Palettes (6-bit VGA DAC values). */
static PALETTE s_game_palette;      /* Set by the game via Video_SetPalette. */
static PALETTE s_bene_palette;
static PALETTE s_hw_palette;        /* What the DAC currently holds. */
static bool s_frame_uses_bene;      /* A BENE.PAL image was drawn this frame. */

/* Nearest colour lookups, built from IBM.PAL. */
static RGB_MAP s_rgb_map;
static bool s_have_rgb_map;
static unsigned char s_shade[SHADE_LEVELS][256];  /* Darken towards black. */
static unsigned char s_brighten[256];               /* Additive highlight. */

static CPSStore *s_cps;
static BITMAP *scratch;
static BITMAP *interface_texture;   /* cps specials and fonts. */
static BITMAP *icon_texture;        /* 16x16 tiles. */
static BITMAP *shape_texture;       /* in game shapes. */
static BITMAP *mentat_texture;
static BITMAP *region_texture;      /* strategic map shapes. */
static BITMAP *s_wsa_static[WSA_STATIC_MAX];
static IconCoord s_icon[ICONID_MAX][HOUSE_NEUTRAL];
static unsigned char s_icon_kind[ICONID_MAX][HOUSE_NEUTRAL];
static unsigned char s_icon_colour[ICONID_MAX][HOUSE_NEUTRAL];
static BITMAP *s_shape[SHAPEID_MAX][HOUSE_NEUTRAL];
static bool s_shape_bene[SHAPEID_MAX];
static BITMAP *s_font[FONTID_MAX][256];

static BITMAP *s_minimap;
static int s_minimap_colour[MAP_SIZE_MAX * MAP_SIZE_MAX];

/* Sub-bitmaps cut from the atlases, so that the atlases can be trimmed
 * to the rows actually used once everything has been exported. */
static struct {
	BITMAP *sub;
	BITMAP *parent;
} s_subs[4096];
static int s_num_subs;

/* F10 cycles the performance overlay: off, frame rate, everything. */
enum Overlay {
	OVERLAY_OFF,
	OVERLAY_FPS,
	OVERLAY_FULL,

	OVERLAY_MAX
};

#define OVERLAY_LINES       5

static enum Overlay s_overlay;
static char s_overlay_text[OVERLAY_LINES][80];
static BITMAP *s_overlay_bmp;

static void VideoDOS_RenderOverlay(void);
static void VideoDOS_IdleOverlay(void);

/* The mouse cursor is drawn straight onto the screen, not into frames,
 * so it follows the mouse between frames and partway through drawing
 * one.  s_front (what is on screen, without the cursor) restores what
 * was under it.  Without s_front (DD_PRESENT=full) it is drawn into
 * each frame instead. */
static BITMAP *s_cursor_bmp;            /* Cursor composed over the background. */
static bool s_cursor_shown;
static int s_cursor_rect[4];            /* Shown on screen: x, y, w, h. */
static int s_cursor_mx, s_cursor_my, s_cursor_sprite;
static int s_cursor_poll;               /* Blits until the next mouse check. */

static void VideoDOS_UpdateCursor(bool force);
static void VideoDOS_UpdateIntTransform(void);
static BITMAP *s_terrain;               /* Viewport terrain layer (terraincache_dos.h). */

/* After an in-game frame the back buffer is not cleared at once: the
 * next in-game frame repaints every pixel (UI cache, terrain layer).
 * Anything else clears it on its first draw (VideoDOS_TouchBackbuffer). */
static bool s_pending_clear;
static bool s_frame_ingame;             /* This frame began with the in-game UI. */
static bool s_clear_skipped;            /* This frame relies on repainting everything. */
static long s_deferred_clears;          /* DD_PROFILE: deferred clears that had to happen. */

static inline void
VideoDOS_TouchBackbuffer(void)
{
	if (s_pending_clear) {
		s_pending_clear = false;
		s_deferred_clears++;
		if (g_dos_backbuffer != NULL)
			clear_bitmap(g_dos_backbuffer);
	}
}

/* Menubar and sidebar cache (uicache_dos.h). */
#define UI_CACHE_TICKS      4       /* Redraw at least every 4/60 s. */

static BITMAP *s_ui_cache;
static bool s_ui_valid;
static int64_t s_ui_tick;
static unsigned int s_ui_serial;
static int s_ui_rect[2][4];         /* Menubar, sidebar: x1, y1, x2, y2. */
static PerfSnapshot s_overlay_last;
static FadeInAux s_fadeInAux;

/* Scripted test hooks (environment), in GUI timer ticks (1/60 s):
 *   DD_AUTOSHOT=<tick>[,<tick>...]  save SHOTnnnn.PCX of the screen.
 *   DD_AUTOEXIT=<tick>              print statistics and exit.
 * Ticks rather than frames: idle menus do not redraw.
 */
static long s_frame;
static const char *s_autoshot_next;
static long s_autoexit_tick = -1;
static int64_t s_first_frame_tick;

/* DD_PROFILE=<tick>: print timings at DD_AUTOEXIT, counted from <tick>.
 * Times are PIT ticks from perf_dos.c. */
static bool s_profiling;
static long s_profile_start_tick;   /* Counters reset at this tick. */
static bool s_profile_started;
static PerfSnapshot s_profile_base;
static long s_blit_calls[BLIT_SHIFT + 1];
static long s_blit_pixels[BLIT_SHIFT + 1];
static long s_blit_slow_calls[BLIT_SHIFT + 1];  /* Not an integer-scale fast path. */
static long s_blit_slow_pixels[BLIT_SHIFT + 1];
static uint64_t s_fill_ticks;       /* Total time in solid icon fills. */

/* Timing each blit costs a few microseconds of port I/O, so only for
 * DD_PROFILE. */
static bool s_time_blits;

/* Per mission summaries in DUNEDYN.LOG. */
static PerfSnapshot s_mission_base;
static bool s_mission_running;

/* Diff present (DD_PRESENT=diff): copy of what is on screen, so only
 * changed spans of each scanline are sent to video memory. */
static BITMAP *s_front;

static struct DisplayMode s_display_modes[] = {
	{ 320, 200 },
	{ 320, 240 },
	{ 640, 400 },
	{ 640, 480 },
	{ 800, 600 },
	{ 1024, 768 },
};

/*--------------------------------------------------------------*/

static void
VideoDOS_ApplyClip(void)
{
	if (g_dos_backbuffer == NULL)
		return;

	set_clip_rect(g_dos_backbuffer,
			s_div_bitmap[s_div].cx1, s_div_bitmap[s_div].cy1,
			s_div_bitmap[s_div].cx2 - 1, s_div_bitmap[s_div].cy2 - 1);
}

void
VideoDOS_SetDivBitmap(enum ScreenDivID div, int x, int y, int w, int h)
{
	s_div_bitmap[div].x = x;
	s_div_bitmap[div].y = y;
	s_div_bitmap[div].w = w;
	s_div_bitmap[div].h = h;

	/* A new sub-bitmap starts unclipped. */
	s_div_bitmap[div].cx1 = x;
	s_div_bitmap[div].cy1 = y;
	s_div_bitmap[div].cx2 = x + w;
	s_div_bitmap[div].cy2 = y + h;

	if (div == s_div) {
		VideoDOS_UpdateIntTransform();
		VideoDOS_ApplyClip();
	}
}

/* The current ScreenDiv as an integer transform, when its scale is a
 * whole number (1 or 2 in practice): screen = origin + k * position.
 * Same results as VideoDOS_ToScreen without float conversions, which
 * cost a 486 about 50 cycles each. */
static bool s_int_ok;
static int s_int_ox, s_int_oy, s_int_kx, s_int_ky;

static void
VideoDOS_UpdateIntTransform(void)
{
	if (s_div == SCREENDIV_MAIN) {
		s_int_ok = true;
		s_int_ox = s_int_oy = 0;
		s_int_kx = s_int_ky = 1;
		return;
	}

	const ScreenDiv *div = &g_screenDiv[s_div];
	s_int_kx = (int)div->scalex;
	s_int_ky = (int)div->scaley;
	s_int_ok = (s_int_kx >= 1 && s_int_ky >= 1 && (float)s_int_kx == div->scalex && (float)s_int_ky == div->scaley);

	if (s_div == SCREENDIV_MENU || s_div == SCREENDIV_HUD) {
		s_int_ox = div->x;
		s_int_oy = div->y;
	} else {
		s_int_ox = s_div_bitmap[s_div].x;
		s_int_oy = s_div_bitmap[s_div].y;
	}
}

void
VideoDOS_SetTransform(enum ScreenDivID div)
{
	s_div = div;
	VideoDOS_UpdateIntTransform();
	VideoDOS_ApplyClip();
}

/* Map coordinates in the current ScreenDiv to back buffer pixels. */
void
VideoDOS_ToScreen(float x, float y, int *sx, int *sy)
{
	VideoDOS_TouchBackbuffer();

	if (s_div == SCREENDIV_MAIN) {
		*sx = (int)x;
		*sy = (int)y;
	} else if (s_div == SCREENDIV_MENU || s_div == SCREENDIV_HUD) {
		/* Full screen sub-bitmap with an offset transform. */
		const ScreenDiv *div = &g_screenDiv[s_div];
		*sx = div->x + (int)(x * div->scalex);
		*sy = div->y + (int)(y * div->scaley);
	} else {
		/* Sub-bitmap at the div's (shaken) position, scale transform. */
		const ScreenDiv *div = &g_screenDiv[s_div];
		*sx = s_div_bitmap[s_div].x + (int)(x * div->scalex);
		*sy = s_div_bitmap[s_div].y + (int)(y * div->scaley);
	}
}

void
VideoDOS_ScaleToScreen(float w, float h, int *sw, int *sh)
{
	if (s_div == SCREENDIV_MAIN) {
		*sw = (int)w;
		*sh = (int)h;
	} else {
		const ScreenDiv *div = &g_screenDiv[s_div];
		*sw = (int)(w * div->scalex);
		*sh = (int)(h * div->scaley);
	}
}

int
VideoDOS_NearestColour(unsigned char r, unsigned char g, unsigned char b)
{
	if (s_have_rgb_map)
		return s_rgb_map.data[r >> 3][g >> 3][b >> 3];

	return bestfit_color(s_game_palette, r >> 2, g >> 2, b >> 2);
}

/* Shade table for an Allegro 5 style black overlay of the given alpha. */
static const unsigned char *
VideoDOS_ShadeTable(int alpha)
{
	const int level = clamp(0, (alpha * SHADE_LEVELS + 128) / 256, SHADE_LEVELS - 1);
	return s_shade[level];
}

/*--------------------------------------------------------------*/

/**
 * Build nearest colour, shade and highlight tables from a reference
 * palette.  Palette entries that the game animates are excluded so
 * that shaded pixels do not start blinking.
 */
static void
VideoDOS_BuildColourTables(const RGB *ref)
{
	PALETTE pal;
	memcpy(pal, ref, sizeof(pal));

	static const int animated[] = { 6, WINDTRAP_COLOUR };
	for (unsigned int i = 0; i < lengthof(animated); i++) {
		pal[animated[i]].r = 63;
		pal[animated[i]].g = 0;
		pal[animated[i]].b = 63;
	}

	create_rgb_table(&s_rgb_map, pal, NULL);
	s_have_rgb_map = true;

	for (int level = 0; level < SHADE_LEVELS; level++) {
		/* Level 0 = untouched, SHADE_LEVELS - 1 = nearly black. */
		const int keep = 256 - (level * 256) / SHADE_LEVELS;

		for (int c = 0; c < 256; c++) {
			const int r = (ref[c].r * keep) >> 8;
			const int g = (ref[c].g * keep) >> 8;
			const int b = (ref[c].b * keep) >> 8;

			s_shade[level][c] = (level == 0) ? c : s_rgb_map.data[r >> 1][g >> 1][b >> 1];
		}
	}

	for (int c = 0; c < 256; c++) {
		const int r = min(63, 2 * ref[c].r);
		const int g = min(63, 2 * ref[c].g);
		const int b = min(63, 2 * ref[c].b);

		s_brighten[c] = s_rgb_map.data[r >> 1][g >> 1][b >> 1];
	}
}

static void
VideoDOS_LoadPalette(const char *filename, RGB *pal)
{
	unsigned char raw[3 * 256];

	File_ReadBlockFile(filename, raw, sizeof(raw));
	for (int i = 0; i < 256; i++) {
		pal[i].r = raw[3 * i + 0] & 0x3F;
		pal[i].g = raw[3 * i + 1] & 0x3F;
		pal[i].b = raw[3 * i + 2] & 0x3F;
	}
}

/*--------------------------------------------------------------*/

static Src8
VideoDOS_SrcFromBitmap(BITMAP *bmp)
{
	Src8 src;

	src.data = bmp->line[0];
	src.pitch = (bmp->h > 1) ? (int)(bmp->line[1] - bmp->line[0]) : bmp->w;
	return src;
}

/* Row copy.  Tile rows are 16 or 32 bytes, where memcpy's call and
 * setup cost more than the copy, so short rows are copied inline. */
static inline void
VideoDOS_CopyRow(unsigned char *d, const unsigned char *s, int w)
{
	if (w > 64) {
		memcpy(d, s, w);
		return;
	}

	int i = 0;
	for (; i + 4 <= w; i += 4) {
		uint32 v;
		memcpy(&v, s + i, 4);
		memcpy(d + i, &v, 4);
	}

	for (; i < w; i++)
		d[i] = s[i];
}

/* True if any byte of a 32-bit word is zero. */
#define HAS_ZERO_BYTE(v)    (((v) - 0x01010101u) & ~(v) & 0x80808080u)

/**
 * Masked row copy, colour 0 transparent.  The 486 cache is
 * write-through, so whole 32-bit writes where possible matter.
 */
static void
VideoDOS_MaskedRow(unsigned char *d, const unsigned char *s, int w)
{
	int i = 0;

	for (; i + 4 <= w; i += 4) {
		uint32 v;
		memcpy(&v, s + i, 4);

		if (v == 0)
			continue;

		if (!HAS_ZERO_BYTE(v)) {
			memcpy(d + i, &v, 4);
			continue;
		}

		if (s[i + 0] != 0) d[i + 0] = s[i + 0];
		if (s[i + 1] != 0) d[i + 1] = s[i + 1];
		if (s[i + 2] != 0) d[i + 2] = s[i + 2];
		if (s[i + 3] != 0) d[i + 3] = s[i + 3];
	}

	for (; i < w; i++) {
		if (s[i] != 0)
			d[i] = s[i];
	}
}

/* Write each source pixel twice: d[0..w) from s, starting half way
 * through a source pixel if odd. */
static void
VideoDOS_DoubleRowOpaque(unsigned char *d, const unsigned char *s, int odd, int w)
{
	int i = 0;

	if (odd && w > 0) {
		d[i++] = *s++;
	}

	for (; i + 2 <= w; i += 2, s++) {
		const uint16 v = (uint16)(*s * 0x0101u);
		memcpy(d + i, &v, 2);
	}

	if (i < w)
		d[i] = *s;
}

/* Masked doubling into one row, or two (d2 != NULL) from the same source. */
static void
VideoDOS_DoubleRowMasked(unsigned char *d, unsigned char *d2, const unsigned char *s, int odd, int w)
{
	int i = 0;

	if (odd && w > 0) {
		if (*s != 0) {
			d[0] = *s;
			if (d2 != NULL) d2[0] = *s;
		}
		i = 1;
		s++;
	}

	for (; i + 2 <= w; i += 2, s++) {
		const unsigned char c = *s;
		if (c == 0)
			continue;

		const uint16 v = (uint16)(c * 0x0101u);
		memcpy(d + i, &v, 2);
		if (d2 != NULL)
			memcpy(d2 + i, &v, 2);
	}

	if (i < w && *s != 0) {
		d[i] = *s;
		if (d2 != NULL) d2[i] = *s;
	}
}

/**
 * Blit at an exact integer scale of 1 or 2 with no flips.  Destination
 * [cx1, cx2) x [cy1, cy2) is already clipped; (x1, y1) is where source
 * pixel (sx, sy) would land.
 */
static void
VideoDOS_BlitScaledInt(const Src8 *src, int sx, int sy, int x1, int y1,
		int cx1, int cy1, int cx2, int cy2, int kx, int ky,
		enum BlitMode mode, const unsigned char *lut, int colour)
{
	BITMAP *dst = g_dos_backbuffer;
	const int out_w = cx2 - cx1;

	for (int y = cy1; y < cy2; y++) {
		const unsigned char *s = src->data + src->pitch * (sy + (y - y1) / ky) + sx;
		unsigned char *d = dst->line[y] + cx1;

		if (kx == 1) {
			s += cx1 - x1;

			switch (mode) {
				case BLIT_OPAQUE:
					VideoDOS_CopyRow(d, s, out_w);
					break;

				case BLIT_MASKED:
					VideoDOS_MaskedRow(d, s, out_w);
					break;

				case BLIT_LUT:
					for (int i = 0; i < out_w; i++) {
						const unsigned char c = s[i];
						if (c != 0)
							d[i] = lut[c];
					}
					break;

				case BLIT_SOLID:
					for (int i = 0; i < out_w; i++) {
						if (s[i] != 0)
							d[i] = colour;
					}
					break;

				case BLIT_SHADE:
					for (int i = 0; i < out_w; i++) {
						if (s[i] != 0)
							d[i] = lut[d[i]];
					}
					break;

				case BLIT_SHIFT:
					for (int i = 0; i < out_w; i++) {
						if (s[i] != 0 && cx1 + i + colour < dst->cr)
							d[i] = d[i + colour];
					}
					break;
			}
		} else {
			/* kx == 2: each source pixel covers two destination pixels,
			 * and with ky == 2 two destination rows. */
			const int off = cx1 - x1;
			const bool pair = (ky == 2) && (((y - y1) & 1) == 0) && (y + 1 < cy2);
			unsigned char *d2 = pair ? dst->line[y + 1] + cx1 : NULL;

			s += off >> 1;

			if (mode == BLIT_OPAQUE) {
				VideoDOS_DoubleRowOpaque(d, s, off & 1, out_w);
				if (pair)
					memcpy(d2, d, out_w);
			} else if (mode == BLIT_MASKED) {
				VideoDOS_DoubleRowMasked(d, d2, s, off & 1, out_w);
			} else {
				/* Rare modes: per pixel. */
				for (int i = 0; i < out_w; i++) {
					const unsigned char c = s[((off & 1) + i) >> 1];
					if (c == 0)
						continue;

					for (int r = 0; r < (pair ? 2 : 1); r++) {
						unsigned char *dd = (r == 0) ? d : d2;

						switch (mode) {
							case BLIT_LUT:   dd[i] = lut[c]; break;
							case BLIT_SOLID: dd[i] = colour; break;
							case BLIT_SHADE: dd[i] = lut[dd[i]]; break;
							case BLIT_SHIFT:
								if (cx1 + i + colour < dst->cr)
									dd[i] = dd[i + colour];
								break;
							default: break;
						}
					}
				}
			}

			if (pair)
				y++;
		}
	}
}

/**
 * The core blitter.  Copies src region (sx, sy, sw, sh) to the
 * current ScreenDiv rectangle (dx, dy, dw, dh), nearest neighbour,
 * clipped to the back buffer clip rectangle.
 * flip: bit 0 horizontal, bit 1 vertical.
 */
static void
VideoDOS_BlitRect(const Src8 *src, int sx, int sy, int sw, int sh,
		int x1, int y1, int x2, int y2, int flip,
		enum BlitMode mode, const unsigned char *lut, int colour)
{
	VideoDOS_TouchBackbuffer();

	BITMAP *dst = g_dos_backbuffer;

	if (sw <= 0 || sh <= 0)
		return;

	const int full_w = x2 - x1;
	const int full_h = y2 - y1;
	if (full_w <= 0 || full_h <= 0)
		return;

	const int cx1 = max(x1, dst->cl);
	const int cy1 = max(y1, dst->ct);
	const int cx2 = min(x2, dst->cr);
	const int cy2 = min(y2, dst->cb);
	if (cx1 >= cx2 || cy1 >= cy2)
		return;

	const int out_w = cx2 - cx1;

	s_blit_calls[mode]++;
	s_blit_pixels[mode] += out_w * (cy2 - cy1);

	/* Fast path: no flips and an exact integer scale (1:1 tiles and
	 * sprites, 2:1 for 320x200 art on a 640 wide screen). */
	const int kx = full_w / sw;
	const int ky = full_h / sh;

	if (flip == 0 && kx >= 1 && ky >= 1 && kx * sw == full_w && ky * sh == full_h && kx <= 2) {
		VideoDOS_BlitScaledInt(src, sx, sy, x1, y1, cx1, cy1, cx2, cy2, kx, ky, mode, lut, colour);
		return;
	}

	s_blit_slow_calls[mode]++;
	s_blit_slow_pixels[mode] += out_w * (cy2 - cy1);

	/* Source column for each destination column. */
	static int xmap[2048];
	assert(out_w <= (int)lengthof(xmap));

	for (int i = 0; i < out_w; i++) {
		int u = ((cx1 - x1 + i) * sw) / full_w;
		if (flip & 0x01)
			u = sw - 1 - u;
		xmap[i] = sx + u;
	}

	for (int y = cy1; y < cy2; y++) {
		int v = ((y - y1) * sh) / full_h;
		if (flip & 0x02)
			v = sh - 1 - v;

		const unsigned char *s = src->data + src->pitch * (sy + v);
		unsigned char *d = dst->line[y] + cx1;

		switch (mode) {
			case BLIT_OPAQUE:
				for (int i = 0; i < out_w; i++)
					d[i] = s[xmap[i]];
				break;

			case BLIT_MASKED:
				for (int i = 0; i < out_w; i++) {
					const unsigned char c = s[xmap[i]];
					if (c != 0)
						d[i] = c;
				}
				break;

			case BLIT_LUT:
				for (int i = 0; i < out_w; i++) {
					const unsigned char c = s[xmap[i]];
					if (c != 0)
						d[i] = lut[c];
				}
				break;

			case BLIT_SOLID:
				for (int i = 0; i < out_w; i++) {
					if (s[xmap[i]] != 0)
						d[i] = colour;
				}
				break;

			case BLIT_SHADE:
				for (int i = 0; i < out_w; i++) {
					if (s[xmap[i]] != 0)
						d[i] = lut[d[i]];
				}
				break;

			case BLIT_SHIFT:
				/* Left to right, so the pixels read are not yet shifted. */
				for (int i = 0; i < out_w; i++) {
					if (s[xmap[i]] != 0 && cx1 + i + colour < dst->cr)
						d[i] = d[i + colour];
				}
				break;
		}
	}
}

/* Float coordinates in the current ScreenDiv. */
static void
VideoDOS_BlitSrc(const Src8 *src, int sx, int sy, int sw, int sh,
		float dx, float dy, float dw, float dh, int flip,
		enum BlitMode mode, const unsigned char *lut, int colour)
{
	int x1, y1, x2, y2;

	VideoDOS_ToScreen(dx, dy, &x1, &y1);
	VideoDOS_ToScreen(dx + dw, dy + dh, &x2, &y2);
	VideoDOS_BlitRect(src, sx, sy, sw, sh, x1, y1, x2, y2, flip, mode, lut, colour);
}

/* Shared by every draw: keep the cursor moving during slow frames. */
static inline void
VideoDOS_BlitPoll(void)
{
	if (--s_cursor_poll < 0) {
		s_cursor_poll = 32;
		VideoDOS_UpdateCursor(false);
	}
}

static void
VideoDOS_Blit(BITMAP *bmp, int sx, int sy, int sw, int sh,
		float dx, float dy, float dw, float dh, int flip,
		enum BlitMode mode, const unsigned char *lut, int colour)
{
	if (bmp == NULL)
		return;

	const Src8 src = VideoDOS_SrcFromBitmap(bmp);
	VideoDOS_BlitPoll();

	if (s_time_blits) {
		const uint64_t t0 = Perf_Now();
		VideoDOS_BlitSrc(&src, sx, sy, sw, sh, dx, dy, dw, dh, flip, mode, lut, colour);
		Perf_Add(PERF_BLIT, (uint32_t)(Perf_Now() - t0));
	} else {
		VideoDOS_BlitSrc(&src, sx, sy, sw, sh, dx, dy, dw, dh, flip, mode, lut, colour);
	}
}

/**
 * Source region (sx, sy, sw, sh) at its natural size, at integer
 * position (x, y) in the current ScreenDiv: the common case (tiles,
 * sprites, glyphs), without float conversions when the scale is whole.
 */
static void
VideoDOS_BlitAt(BITMAP *bmp, int sx, int sy, int sw, int sh, int x, int y,
		int flip, enum BlitMode mode, const unsigned char *lut, int colour)
{
	if (!s_int_ok || s_time_blits) {
		VideoDOS_Blit(bmp, sx, sy, sw, sh, x, y, sw, sh, flip, mode, lut, colour);
		return;
	}

	if (bmp == NULL)
		return;

	const Src8 src = VideoDOS_SrcFromBitmap(bmp);
	VideoDOS_BlitPoll();

	const int x1 = s_int_ox + x * s_int_kx;
	const int y1 = s_int_oy + y * s_int_ky;
	VideoDOS_BlitRect(&src, sx, sy, sw, sh, x1, y1, x1 + sw * s_int_kx, y1 + sh * s_int_ky,
			flip, mode, lut, colour);
}

/* Draw a whole bitmap at its natural size in the current ScreenDiv. */
static void
VideoDOS_Draw(BITMAP *bmp, int x, int y, int flip, enum BlitMode mode, const unsigned char *lut, int colour)
{
	if (bmp == NULL)
		return;

	VideoDOS_BlitAt(bmp, 0, 0, bmp->w, bmp->h, x, y, flip, mode, lut, colour);
}

/*--------------------------------------------------------------*/

/* Atlas packing, as VideoA5_GetNextXY. */
static void
VideoDOS_GetNextXY(int texture_width, int texture_height,
		int x, int y, int w, int h, int row_h, int *retx, int *rety)
{
	if (x + w - 1 >= texture_width) {
		x = 1;
		y += row_h + 1;
	}

	if (y + h - 1 >= texture_height) {
		Error("Texture atlas %dx%d overflowed.\n", texture_width, texture_height);
		exit(1);
	}

	*retx = x;
	*rety = y;
}

/* Copy 8bpp game data into a bitmap, as VideoA5_CopyBitmap.  Colour 0
 * stays 0: transparent when drawn masked, black when drawn opaque. */
static void
VideoDOS_CopyBitmap(int src_stride, const unsigned char *raw, BITMAP *dest, enum BitmapCopyMode mode)
{
	for (int y = 0; y < dest->h; y++) {
		const unsigned char *s = raw + src_stride * y;
		unsigned char *d = dest->line[y];

		if (mode == SKIP_COLOUR_0) {
			for (int x = 0; x < dest->w; x++) {
				if (s[x] != 0)
					d[x] = s[x];
			}
		} else {
			memcpy(d, s, dest->w);
		}
	}
}

static void
VideoDOS_CreateWhiteMaskIndexed(unsigned char *buf,
		int stride, int sx, int sy, int dx, int dy, int w, int h, int ref)
{
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int src = stride * (sy + y) + (sx + x);
			const int dst = stride * (dy + y) + (dx + x);

			buf[dst] = (buf[src] == ref) ? 0xFF : 0x00;
		}
	}
}

static BITMAP *
VideoDOS_SubBitmap(BITMAP *parent, int x, int y, int w, int h)
{
	BITMAP *sub = create_sub_bitmap(parent, x, y, w, h);
	assert(sub != NULL);

	if (s_num_subs < (int)lengthof(s_subs)) {
		s_subs[s_num_subs].sub = sub;
		s_subs[s_num_subs].parent = parent;
		s_num_subs++;
	}

	return sub;
}

static void
VideoDOS_ForgetSubBitmaps(BITMAP *parent)
{
	for (int i = 0; i < s_num_subs; i++) {
		if (s_subs[i].parent == parent)
			s_subs[i].parent = NULL;
	}
}

/**
 * Shrink an atlas to the rows actually used, moving its sub-bitmaps
 * into the new memory.  The atlases are allocated at the full render
 * texture size (1024x1024) for packing, but are mostly much smaller.
 */
static void
VideoDOS_TrimAtlas(BITMAP **atlas)
{
	BITMAP *old = *atlas;
	int used = 0;

	for (int y = 0; y < old->h; y++) {
		const unsigned char *row = old->line[y];

		for (int x = 0; x < old->w; x++) {
			if (row[x] != 0) {
				used = y + 1;
				break;
			}
		}
	}

	/* Sub-bitmaps may extend below the last non-empty row. */
	for (int i = 0; i < s_num_subs; i++) {
		if (s_subs[i].parent == old)
			used = max(used, s_subs[i].sub->y_ofs + s_subs[i].sub->h);
	}

	if (used <= 0 || used >= old->h)
		return;

	BITMAP *trimmed = create_bitmap_ex(8, old->w, used);
	if (trimmed == NULL)
		return;

	blit(old, trimmed, 0, 0, 0, 0, old->w, used);

	for (int i = 0; i < s_num_subs; i++) {
		BITMAP *sub = s_subs[i].sub;
		if (s_subs[i].parent != old)
			continue;

		for (int j = 0; j < sub->h; j++)
			sub->line[j] = trimmed->line[sub->y_ofs + j] + sub->x_ofs;

		s_subs[i].parent = trimmed;
	}

	destroy_bitmap(old);
	*atlas = trimmed;
}

static BITMAP *
VideoDOS_CreateBitmap(int w, int h)
{
	BITMAP *bmp = create_bitmap_ex(8, w, h);
	if (bmp == NULL) {
		Error("Could not allocate %dx%d bitmap.\n", w, h);
		exit(1);
	}

	clear_bitmap(bmp);
	return bmp;
}

/*--------------------------------------------------------------*/

bool
VideoA5_Init(void)
{
	int w = g_gameConfig.displayMode.width;
	int h = g_gameConfig.displayMode.height;

	if (w <= 0 || h <= 0) {
		w = 640;
		h = 480;
	}

	set_color_depth(8);
	if (set_gfx_mode(GFX_AUTODETECT, w, h, 0, 0) != 0) {
		Error("Could not set %dx%d 8bpp video mode: %s\n", w, h, allegro_error);

		w = 320;
		h = 200;
		if (set_gfx_mode(GFX_VGA, w, h, 0, 0) != 0) {
			Error("Could not set 320x200 VGA mode: %s\n", allegro_error);
			return false;
		}
	}

	TRUE_DISPLAY_WIDTH = w;
	TRUE_DISPLAY_HEIGHT = h;

	/* "VESA 1.x" is banked; "VESA 2.0 (linear)" is the fast path, which
	 * cards without VBE 2.0 BIOS support get from UniVBE / SciTech. */
	RunLog_SetGraphicsMode(true);
	RunLog_Printf("Video: %dx%d 8bpp, %s (%s)\n", w, h, gfx_driver->name, gfx_driver->desc);
	if (w >= 640 && gfx_driver->linear == 0 && !is_planar_bitmap(screen))
		RunLog_Printf("Video: banked video memory; VESA 2.0 (UniVBE) gives a faster linear frame buffer\n");

	g_dos_backbuffer = VideoDOS_CreateBitmap(w, h);

	const int tw = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	const int th = g_widgetProperties[WINDOWID_RENDER_TEXTURE].height;
	interface_texture = VideoDOS_CreateBitmap(tw, th);
	shape_texture = VideoDOS_CreateBitmap(tw, th);
	region_texture = VideoDOS_CreateBitmap(tw, th);
	s_minimap = VideoDOS_CreateBitmap(64, 64);

	s_autoshot_next = getenv("DD_AUTOSHOT");

	const char *env = getenv("DD_AUTOEXIT");
	if (env != NULL)
		s_autoexit_tick = atol(env);

	/* Only send changed scanline spans to video memory, unless
	 * DD_PRESENT=full.  Comparing costs less than sending on every
	 * machine measured (486 VLB/PCI, Pentium PCI). */
	env = getenv("DD_PRESENT");
	if (env == NULL || strcmp(env, "full") != 0) {
		s_front = VideoDOS_CreateBitmap(w, h);
		const int size = (w >= 640) ? 32 : 16;
		s_cursor_bmp = create_bitmap_ex(8, size, size);
	} else {
		RunLog_Printf("Present: full screen\n");
	}

	if (getenv("DD_PROFILE") != NULL) {
		s_profile_start_tick = atol(getenv("DD_PROFILE"));
		s_profiling = true;
		/* -profcoarse: frame totals only, without the cost of timing
		 * every blit and draw section. */
		g_perf_detail = !DOS_HasArg("profcoarse");
	}

	s_overlay = DOS_HasArg("overlay") ? OVERLAY_FULL : OVERLAY_OFF;
	s_time_blits = g_perf_detail;

	return true;
}

static void
VideoDOS_UninitCPSStore(void)
{
	while (s_cps != NULL) {
		CPSStore *next = s_cps->next;

		destroy_bitmap(s_cps->bmp);
		free(s_cps);

		s_cps = next;
	}

	s_fadeInAux.bmp = NULL;
}

void
VideoA5_Uninit(void)
{
	/* Sub bitmaps first, then the textures they point into. */
	for (enum ShapeID shapeID = 0; shapeID < SHAPEID_MAX; shapeID++) {
		for (enum HouseType houseID = HOUSE_HARKONNEN; houseID < HOUSE_NEUTRAL; houseID++) {
			if (s_shape[shapeID][houseID] == NULL)
				continue;

			if ((houseID + 1 == HOUSE_NEUTRAL) || (s_shape[shapeID][houseID] != s_shape[shapeID][houseID + 1]))
				destroy_bitmap(s_shape[shapeID][houseID]);

			s_shape[shapeID][houseID] = NULL;
		}
	}

	for (int fnt = 0; fnt < FONTID_MAX; fnt++) {
		for (int c = 0; c < 256; c++) {
			if (s_font[fnt][c] != NULL)
				destroy_bitmap(s_font[fnt][c]);
			s_font[fnt][c] = NULL;
		}
	}

	for (int i = 0; i < WSA_STATIC_MAX; i++) {
		if (s_wsa_static[i] != NULL)
			destroy_bitmap(s_wsa_static[i]);
		s_wsa_static[i] = NULL;
	}

	VideoDOS_UninitCPSStore();

	BITMAP **bitmaps[] = {
		&scratch, &s_front, &s_overlay_bmp, &s_cursor_bmp, &s_ui_cache, &s_terrain, &s_minimap, &interface_texture, &icon_texture,
		&shape_texture, &mentat_texture, &region_texture, &g_dos_backbuffer
	};

	for (unsigned int i = 0; i < lengthof(bitmaps); i++) {
		if (*bitmaps[i] != NULL)
			destroy_bitmap(*bitmaps[i]);
		*bitmaps[i] = NULL;
	}

	free(s_fadeInAux.revealed);
	s_fadeInAux.revealed = NULL;

	set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
	RunLog_SetGraphicsMode(false);
}

void
VideoA5_ToggleFullscreen(void)
{
}

void
VideoA5_ToggleFPS(void)
{
	s_overlay = (s_overlay + 1) % OVERLAY_MAX;

	/* Start a fresh measurement interval. */
	snprintf(s_overlay_text[0], sizeof(s_overlay_text[0]), "measuring");
	for (int i = 1; i < OVERLAY_LINES; i++)
		s_overlay_text[i][0] = '\0';
	Perf_Snapshot(&s_overlay_last, PERF_WINDOW_OVERLAY);
	VideoDOS_RenderOverlay();
}

void
VideoA5_CaptureScreenshot(void)
{
	char filename[1024];

	for (int i = 0; i < 10000; i++) {
		snprintf(filename, sizeof(filename), "%sSHOT%04d.PCX", g_personal_data_dir, i);
		if (!exists(filename))
			break;
	}

	/* The back buffer is cleared after each present, so save what is
	 * on screen. */
	BITMAP *bmp = create_bitmap_ex(8, SCREEN_W, SCREEN_H);
	if (bmp == NULL)
		return;

	acquire_screen();
	blit(screen, bmp, 0, 0, 0, 0, SCREEN_W, SCREEN_H);
	release_screen();

	if (save_pcx(filename, bmp, s_hw_palette) != 0)
		Error("Could not save %s\n", filename);

	destroy_bitmap(bmp);
}

void
VideoDOS_PollAutomation(void)
{
	VideoDOS_IdleOverlay();
	VideoDOS_UpdateCursor(false);

	static bool s_have_autoinput;
	static bool s_init;

	if (!s_init) {
		s_have_autoinput = (getenv("DD_AUTOINPUT") != NULL);
		s_init = true;
	}

	if (s_autoshot_next == NULL && s_autoexit_tick < 0 && !s_have_autoinput && !s_profiling)
		return;

	if (!Timer_IsStarted(TIMER_GUI))
		return;

	const int64_t ticks = Timer_GetTicks();

	InputDOS_PollAutomation(ticks);

	if (s_autoshot_next != NULL && *s_autoshot_next != '\0' && ticks >= atol(s_autoshot_next)) {
		VideoA5_CaptureScreenshot();
		printf("DD_AUTOSHOT: tick %ld, frame %ld, game timer %s at %ld\n", (long)ticks, s_frame,
			Timer_IsStarted(TIMER_GAME) ? "running" : "stopped", (long)Timer_GameTicks());

		s_autoshot_next = strchr(s_autoshot_next, ',');
		if (s_autoshot_next != NULL)
			s_autoshot_next++;
	}

	if (s_profiling && !s_profile_started && ticks >= s_profile_start_tick) {
		s_profile_started = true;
		s_frame = 0;
		Perf_Snapshot(&s_profile_base, PERF_WINDOW_PROFILE);
		s_fill_ticks = 0;
		s_deferred_clears = 0;
		memset(s_blit_calls, 0, sizeof(s_blit_calls));
		memset(s_blit_slow_calls, 0, sizeof(s_blit_slow_calls));
		memset(s_blit_slow_pixels, 0, sizeof(s_blit_slow_pixels));
		memset(s_blit_pixels, 0, sizeof(s_blit_pixels));
		printf("DD_PROFILE: started at tick %ld\n", (long)ticks);
	}

	if (s_autoexit_tick >= 0 && ticks >= s_autoexit_tick) {
		const double secs = (ticks - s_first_frame_tick) / 60.0;
		printf("DD_AUTOEXIT: tick %ld, %ld frames in %.2f s, %.1f fps\n",
			(long)ticks, s_frame, secs, (secs > 0.0 && s_frame > 1) ? (s_frame - 1) / secs : 0.0);

		if (s_profiling && s_profile_started) {
			PerfSnapshot now;
			Perf_Snapshot(&now, PERF_WINDOW_PROFILE);

			const double frames = now.frames - s_profile_base.frames;
			const int pixels = g_dos_backbuffer->w * g_dos_backbuffer->h;
#define PROFILE_MS(c)   (frames > 0 ? Perf_TicksToMs(now.ticks[c] - s_profile_base.ticks[c]) / frames : 0.0)

			if (frames > 0) {
				const double present = PROFILE_MS(PERF_PRESENT);
				const double bytes = (now.present_bytes - s_profile_base.present_bytes) / frames;

				printf("DD_PROFILE: %.0f frames, %.2f ms/frame, worst %.1f ms, %.0f%% game speed\n",
					frames, Perf_TicksToMs(now.time - s_profile_base.time) / frames,
					Perf_TicksToMs(now.worst_frame),
					100.0 * (now.game_ticks - s_profile_base.game_ticks) / (Perf_TicksToMs(now.time - s_profile_base.time) / 1000.0 * TimerDOS_GameHz()));
				printf("DD_PROFILE: GUI ticks %.2f, game ticks %.2f, draw %.2f (blitter %.2f, solid icon fills %.2f), clear %.2f ms/frame\n",
					PROFILE_MS(PERF_GUI_TICKS), PROFILE_MS(PERF_GAME_TICKS), PROFILE_MS(PERF_DRAW),
					PROFILE_MS(PERF_BLIT), Perf_TicksToMs(s_fill_ticks) / frames, PROFILE_MS(PERF_CLEAR));
				printf("DD_PROFILE: present %.2f ms/frame %dx%d, %.0f bytes/frame (%.0f%% of the screen, %.1f MB/s)\n",
					present, g_dos_backbuffer->w, g_dos_backbuffer->h, bytes, 100.0 * bytes / pixels,
					(present > 0.0) ? bytes / present / 1048.576 : 0.0);
				static const struct { enum PerfCounter c; const char *name; } parts[] = {
					{ PERF_DRAW_MENUBAR, "menubar" }, { PERF_DRAW_SIDEBAR, "sidebar" },
					{ PERF_DRAW_FOGUPDATE, "fog update" }, { PERF_DRAW_TILES, "tiles" },
					{ PERF_DRAW_UNITS, "units" }, { PERF_DRAW_EXPLOSIONS, "explosions" },
					{ PERF_DRAW_FOG, "fog edges" }, { PERF_DRAW_MARKERS, "markers" },
					{ PERF_DRAW_AIR, "air units" }, { PERF_DRAW_UNITMAP, "unit map" },
					{ PERF_DRAW_MINIMAP, "(minimap)" }, { PERF_DRAW_VIEWPORT, "(viewport)" },
					{ PERF_DRAW_CHAT, "chat" }, { PERF_DRAW_SCREEN, "(drawscreen)" },
					{ PERF_DRAW_INTERFACE, "(interface)" },
					{ PERF_UNIT_FOGRADIUS, "[unit fog radius]" }, { PERF_UNIT_AROUND, "[unit around]" },
				};
				printf("DD_PROFILE: draw parts, ms/frame:");
				for (unsigned int i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
					printf(" %s %.2f", parts[i].name, PROFILE_MS(parts[i].c));
				printf("\n");
				printf("DD_PROFILE: %.2f deferred back buffer clears per frame\n", s_deferred_clears / frames);
				printf("DD_PROFILE: music interrupts %.2f%% of the CPU\n",
					100.0 * (now.ticks[PERF_IRQ_MUSIC] - s_profile_base.ticks[PERF_IRQ_MUSIC]) / (double)(now.time - s_profile_base.time));

				static const char *names[] = { "opaque", "masked", "lut", "solid", "shade", "shift" };
				for (int m = 0; m <= BLIT_SHIFT; m++) {
					if (s_blit_calls[m] == 0)
						continue;
					printf("DD_PROFILE:   %-6s %6.1f calls/frame %8.0f pixels/frame, slow path %5.1f calls %7.0f pixels\n",
						names[m], s_blit_calls[m] / frames, s_blit_pixels[m] / frames,
						s_blit_slow_calls[m] / frames, s_blit_slow_pixels[m] / frames);
				}
			}
#undef PROFILE_MS
		}

		VideoDOS_LogPerformance("Automated run");
		exit(0);
	}
}

void
VideoA5_ClearScreen(void)
{
	s_pending_clear = false;
	clear_bitmap(g_dos_backbuffer);
}

/* Text in the small font, white with its dark outline, into a memory
 * bitmap.  Fixed 6 pixel pitch so that columns line up. */
static void
VideoDOS_OverlayText(BITMAP *dst, int x, int y, int scale, const char *str)
{
	for (; *str != '\0'; str++, x += 6 * scale) {
		const BITMAP *glyph = s_font[0][(unsigned char)*str];

		if (glyph == NULL || *str == ' ')
			continue;

		for (int gy = 0; gy < glyph->h * scale; gy++) {
			const int dy = y + gy;
			if (dy < 0 || dy >= dst->h)
				continue;

			const unsigned char *src = glyph->line[gy / scale];
			unsigned char *d = dst->line[dy];

			for (int gx = 0; gx < glyph->w * scale; gx++) {
				const int dx = x + gx;
				const unsigned char c = src[gx / scale];

				if (c != 0 && dx >= 0 && dx < dst->w)
					d[dx] = (c == 0xFF) ? 15 : c;
			}
		}
	}
}

static void
VideoDOS_FormatOverlay(const PerfSnapshot *a, const PerfSnapshot *b)
{
	const double secs = (double)(b->time - a->time) / PERF_TICKS_PER_SECOND;
	const uint32_t frames = b->frames - a->frames;
	const double f = (frames > 0) ? frames : 1;

#define OVERLAY_MS(c)   (Perf_TicksToMs(b->ticks[c] - a->ticks[c]) / f)

	char speed[24];
	/* Hints, the mentat and the options menu stop the game. */
	if (Timer_IsStarted(TIMER_GAME) && g_gameOverlay == GAMEOVERLAY_NONE) {
		snprintf(speed, sizeof(speed), "speed %.0f%%",
			100.0 * (b->game_ticks - a->game_ticks) / (secs * TimerDOS_GameHz()));
	} else {
		snprintf(speed, sizeof(speed), "paused");
	}

	if (frames == 0) {
		/* Menus only redraw when something changes. */
		snprintf(s_overlay_text[0], sizeof(s_overlay_text[0]), "idle  (screen unchanged)");
	} else {
		snprintf(s_overlay_text[0], sizeof(s_overlay_text[0]), "%.0f fps  %.1f ms  worst %.0f  %s",
			frames / secs, secs * 1000.0 / frames, Perf_TicksToMs(b->worst_frame), speed);
	}

	snprintf(s_overlay_text[1], sizeof(s_overlay_text[1]), "game %.1f  gui %.1f  draw %.1f",
		OVERLAY_MS(PERF_GAME_TICKS), OVERLAY_MS(PERF_GUI_TICKS), OVERLAY_MS(PERF_DRAW));

	const double pixels = (double)g_dos_backbuffer->w * g_dos_backbuffer->h;
	snprintf(s_overlay_text[2], sizeof(s_overlay_text[2]), "clear %.1f  present %.1f  changed %.0f%%",
		OVERLAY_MS(PERF_CLEAR), OVERLAY_MS(PERF_PRESENT), 100.0 * (b->present_bytes - a->present_bytes) / f / pixels);

	int playing, voices;
	AudioDOS_CountVoices(&playing, &voices);
	snprintf(s_overlay_text[3], sizeof(s_overlay_text[3]), "music %.1f%%  sfx %d/%d  free %.1f MB",
		100.0 * (b->ticks[PERF_IRQ_MUSIC] - a->ticks[PERF_IRQ_MUSIC]) / (double)(b->time - a->time),
		playing, voices, _go32_dpmi_remaining_physical_memory() / 1048576.0);

#undef OVERLAY_MS

	if (Perf_CPUMHz() > 0) {
		snprintf(s_overlay_text[4], sizeof(s_overlay_text[4]), "%s %s%d MHz  %dx%d",
			Perf_CPUName(), Perf_CPUMHzExact() ? "" : "~", Perf_CPUMHz(), g_dos_backbuffer->w, g_dos_backbuffer->h);
	} else {
		snprintf(s_overlay_text[4], sizeof(s_overlay_text[4]), "%s  %dx%d",
			Perf_CPUName(), g_dos_backbuffer->w, g_dos_backbuffer->h);
	}
}

/* Render the overlay text into s_overlay_bmp, when it changes. */
static void
VideoDOS_RenderOverlay(void)
{
	const int scale = (g_dos_backbuffer->w >= 1024) ? 2 : 1;
	const int lines = (s_overlay == OVERLAY_FULL) ? OVERLAY_LINES : 1;
	char fps[16];
	const char *text[OVERLAY_LINES];

	for (int i = 0; i < OVERLAY_LINES; i++)
		text[i] = s_overlay_text[i];

	if (s_overlay == OVERLAY_FPS) {
		/* Just the frame rate: the first line up to its first double space. */
		const char *end = strstr(s_overlay_text[0], "  ");
		const int len = (end != NULL) ? (int)(end - s_overlay_text[0]) : (int)strlen(s_overlay_text[0]);
		snprintf(fps, sizeof(fps), "%.*s", len, s_overlay_text[0]);
		text[0] = fps;
	}

	int chars = 1;
	for (int i = 0; i < lines; i++)
		chars = max(chars, (int)strlen(text[i]));

	const int w = min(g_dos_backbuffer->w, (chars * 6 + 2) * scale);
	const int h = min(g_dos_backbuffer->h, (lines * 8 + 1) * scale);

	if (s_overlay_bmp == NULL || s_overlay_bmp->w != w || s_overlay_bmp->h != h) {
		if (s_overlay_bmp != NULL)
			destroy_bitmap(s_overlay_bmp);
		s_overlay_bmp = create_bitmap_ex(8, w, h);
		if (s_overlay_bmp == NULL)
			return;
	}

	clear_to_color(s_overlay_bmp, 0);
	for (int i = 0; i < lines; i++)
		VideoDOS_OverlayText(s_overlay_bmp, scale, (i * 8 + 1) * scale, scale, text[i]);
}

/*--------------------------------------------------------------*/

/* Terrain layer (terraincache_dos.h).  Same size as the back buffer so
 * that coordinates are shared; keys are per map tile. */
static BITMAP *s_terrain_saved_bb;      /* Real back buffer while drawing terrain. */
static TerrainKey s_terrain_key[MAP_SIZE_MAX * MAP_SIZE_MAX];
static uint16 s_terrain_gen[MAP_SIZE_MAX * MAP_SIZE_MAX];
static uint16 s_terrain_curr_gen;
static int s_terrain_geometry[12];
static int s_terrain_rect[4];           /* Viewport on screen: x1, y1, x2, y2. */
static bool s_terrain_active;

static void
VideoDOS_TerrainInvalidate(void)
{
	/* Generation 0 is never current, so a wrap clears everything. */
	if (++s_terrain_curr_gen == 0) {
		memset(s_terrain_gen, 0, sizeof(s_terrain_gen));
		s_terrain_curr_gen = 1;
	}
}

/**
 * Start the viewport tile pass.  mode: anything else that changes how
 * cells are drawn (fog options).
 * @return true if the cache is in use; draws now go to the terrain layer.
 */
bool
VideoDOS_TerrainBegin(int x0, int y0, int viewportX1, int viewportY1, int mode)
{
	if (g_dos_backbuffer == NULL || s_div != SCREENDIV_VIEWPORT || !s_int_ok || s_time_blits) {
		/* The tile pass expects a cleared viewport. */
		if (s_clear_skipped && g_dos_backbuffer != NULL) {
			rectfill(g_dos_backbuffer, s_div_bitmap[s_div].cx1, s_div_bitmap[s_div].cy1,
					s_div_bitmap[s_div].cx2 - 1, s_div_bitmap[s_div].cy2 - 1, 0);
		}
		return false;
	}

	if (s_terrain == NULL || s_terrain->w != g_dos_backbuffer->w || s_terrain->h != g_dos_backbuffer->h) {
		if (s_terrain != NULL)
			destroy_bitmap(s_terrain);
		s_terrain = create_bitmap_ex(8, g_dos_backbuffer->w, g_dos_backbuffer->h);
		if (s_terrain == NULL)
			return false;
		memset(s_terrain_geometry, 0, sizeof(s_terrain_geometry));
		VideoDOS_TerrainInvalidate();
	}

	const int geometry[12] = {
		x0, y0, viewportX1, viewportY1, mode,
		s_int_ox, s_int_oy, s_int_kx, s_int_ky,
		s_div_bitmap[s_div].cx1, s_div_bitmap[s_div].cy1, s_div_bitmap[s_div].cx2 * 4096 + s_div_bitmap[s_div].cy2
	};

	s_terrain_rect[0] = s_div_bitmap[s_div].cx1;
	s_terrain_rect[1] = s_div_bitmap[s_div].cy1;
	s_terrain_rect[2] = s_div_bitmap[s_div].cx2;
	s_terrain_rect[3] = s_div_bitmap[s_div].cy2;

	if (memcmp(geometry, s_terrain_geometry, sizeof(geometry)) != 0) {
		memcpy(s_terrain_geometry, geometry, sizeof(geometry));
		VideoDOS_TerrainInvalidate();

		/* Outside the map stays as cleared. */
		rectfill(s_terrain, s_terrain_rect[0], s_terrain_rect[1], s_terrain_rect[2] - 1, s_terrain_rect[3] - 1, 0);
	}

	s_terrain_saved_bb = g_dos_backbuffer;
	g_dos_backbuffer = s_terrain;
	VideoDOS_ApplyClip();
	s_terrain_active = true;
	return true;
}

/**
 * @return true if the cell at (left, top) in the viewport must be
 *   drawn: it changed since it was last drawn.  It is then cleared, as
 *   the tile pass expects a cleared buffer.
 */
bool
VideoDOS_TerrainCell(uint16 packed, int left, int top, const TerrainKey *key)
{
	if (!s_terrain_active)
		return true;

	if (s_terrain_gen[packed] == s_terrain_curr_gen && memcmp(&s_terrain_key[packed], key, sizeof(*key)) == 0)
		return false;

	s_terrain_key[packed] = *key;
	s_terrain_gen[packed] = s_terrain_curr_gen;

	const int x1 = max(s_int_ox + left * s_int_kx, s_terrain->cl);
	const int y1 = max(s_int_oy + top * s_int_ky, s_terrain->ct);
	const int x2 = min(s_int_ox + (left + TILE_SIZE) * s_int_kx, s_terrain->cr);
	const int y2 = min(s_int_oy + (top + TILE_SIZE) * s_int_ky, s_terrain->cb);

	if (x1 < x2 && y1 < y2)
		rectfill(s_terrain, x1, y1, x2 - 1, y2 - 1, 0);

	return true;
}

void
VideoDOS_TerrainEnd(void)
{
	if (!s_terrain_active)
		return;

	s_terrain_active = false;
	g_dos_backbuffer = s_terrain_saved_bb;
	VideoDOS_ApplyClip();

	const int *r = s_terrain_rect;
	if (r[2] > r[0] && r[3] > r[1])
		blit(s_terrain, g_dos_backbuffer, r[0], r[1], r[0], r[1], r[2] - r[0], r[3] - r[1]);
}

/*--------------------------------------------------------------*/

static void
VideoDOS_UIRects(int rect[2][4])
{
	static const enum ScreenDivID divs[2] = { SCREENDIV_MENUBAR, SCREENDIV_SIDEBAR };

	for (int i = 0; i < 2; i++) {
		const ScreenDiv *div = &g_screenDiv[divs[i]];

		rect[i][0] = max(0, div->x);
		rect[i][1] = max(0, div->y);
		rect[i][2] = min(g_dos_backbuffer->w, (int)(div->x + div->width * div->scalex + 0.5f));
		rect[i][3] = min(g_dos_backbuffer->h, (int)(div->y + div->height * div->scaley + 0.5f));
	}
}

/**
 * @return true if the menubar and sidebar must be drawn this frame;
 *   otherwise the last drawing has been copied back.
 */
bool
VideoDOS_UIBegin(void)
{
	if (g_dos_backbuffer == NULL)
		return true;

	/* An in-game frame repaints the whole screen: skip the clear. */
	s_frame_ingame = true;
	if (s_pending_clear) {
		s_pending_clear = false;
		s_clear_skipped = true;
	}

	if (s_ui_cache == NULL || s_ui_cache->w != g_dos_backbuffer->w || s_ui_cache->h != g_dos_backbuffer->h) {
		if (s_ui_cache != NULL)
			destroy_bitmap(s_ui_cache);
		s_ui_cache = create_bitmap_ex(8, g_dos_backbuffer->w, g_dos_backbuffer->h);
		s_ui_valid = false;
		if (s_ui_cache == NULL)
			return true;
	}

	int rect[2][4];
	VideoDOS_UIRects(rect);

	const int64_t now = Timer_GetTicks();
	if (!s_ui_valid
			|| g_dos_input_serial != s_ui_serial
			|| now - s_ui_tick >= UI_CACHE_TICKS || now < s_ui_tick
			|| memcmp(rect, s_ui_rect, sizeof(rect)) != 0) {
		if (s_clear_skipped) {
			for (int i = 0; i < 2; i++) {
				if (rect[i][2] > rect[i][0] && rect[i][3] > rect[i][1])
					rectfill(g_dos_backbuffer, rect[i][0], rect[i][1], rect[i][2] - 1, rect[i][3] - 1, 0);
			}
		}
		return true;
	}

	for (int i = 0; i < 2; i++) {
		const int *r = s_ui_rect[i];
		if (r[2] > r[0] && r[3] > r[1])
			blit(s_ui_cache, g_dos_backbuffer, r[0], r[1], r[0], r[1], r[2] - r[0], r[3] - r[1]);
	}

	return false;
}

void
VideoDOS_UIStore(void)
{
	if (s_ui_cache == NULL || g_dos_backbuffer == NULL)
		return;

	VideoDOS_UIRects(s_ui_rect);

	for (int i = 0; i < 2; i++) {
		const int *r = s_ui_rect[i];
		if (r[2] > r[0] && r[3] > r[1])
			blit(g_dos_backbuffer, s_ui_cache, r[0], r[1], r[0], r[1], r[2] - r[0], r[3] - r[1]);
	}

	s_ui_valid = true;
	s_ui_tick = Timer_GetTicks();
	s_ui_serial = g_dos_input_serial;
}

/* Where the cursor should be, in screen pixels. */
static void
VideoDOS_CursorPosition(int *mx, int *my)
{
	if (g_mousePanning) {
		/* The hardware mouse is re-centred while panning; the game
		 * keeps the cursor where panning started. */
		*mx = g_mouseX;
		*my = g_mouseY;
	} else {
		/* Straight from Allegro's mouse interrupt, newer than the last
		 * input the game processed. */
		const int pos = mouse_pos;
		*mx = pos >> 16;
		*my = pos & 0xFFFF;
	}
}

/**
 * Put the cursor on the screen at the mouse position, restoring what
 * was under it from s_front.  Unless forced, only when it has moved,
 * changed shape or been shown or hidden.
 */
static void
VideoDOS_UpdateCursor(bool force)
{
	if (s_front == NULL || s_cursor_bmp == NULL)
		return;

	int mx, my;
	VideoDOS_CursorPosition(&mx, &my);

	const bool show = !g_mouseHidden;
	if (!force && show == s_cursor_shown && mx == s_cursor_mx && my == s_cursor_my && g_cursorSpriteID == s_cursor_sprite)
		return;

	acquire_screen();

	if (s_cursor_shown) {
		const int *r = s_cursor_rect;
		blit(s_front, screen, r[0], r[1], r[0], r[1], r[2], r[3]);
		s_cursor_shown = false;
	}

	s_cursor_mx = mx;
	s_cursor_my = my;
	s_cursor_sprite = g_cursorSpriteID;

	BITMAP *sprite = s_shape[g_cursorSpriteID][HOUSE_HARKONNEN];
	if (show && sprite != NULL) {
		const int size = s_cursor_bmp->w;
		const int x = mx - cursor_focus[g_cursorSpriteID].x * size / 16;
		const int y = my - cursor_focus[g_cursorSpriteID].y * size / 16;
		const int x1 = max(0, x);
		const int y1 = max(0, y);
		const int x2 = min(s_front->w, x + size);
		const int y2 = min(s_front->h, y + size);

		if (x1 < x2 && y1 < y2) {
			/* Background, cursor on top, then one copy to the screen. */
			blit(s_front, s_cursor_bmp, x1, y1, x1 - x, y1 - y, x2 - x1, y2 - y1);
			if (size == 16) {
				masked_blit(sprite, s_cursor_bmp, 0, 0, 0, 0, 16, 16);
			} else {
				masked_stretch_blit(sprite, s_cursor_bmp, 0, 0, 16, 16, 0, 0, size, size);
			}
			blit(s_cursor_bmp, screen, x1 - x, y1 - y, x1, y1, x2 - x1, y2 - y1);

			s_cursor_rect[0] = x1;
			s_cursor_rect[1] = y1;
			s_cursor_rect[2] = x2 - x1;
			s_cursor_rect[3] = y2 - y1;
			s_cursor_shown = true;
		}
	}

	release_screen();
}

/* New numbers twice a second.  Returns true if the text was redone. */
static bool
VideoDOS_UpdateOverlay(void)
{
	const uint64_t now = Perf_Now();
	if (s_overlay_bmp != NULL && now - s_overlay_last.time < PERF_TICKS_PER_SECOND / 2)
		return false;

	PerfSnapshot snap;
	Perf_Snapshot(&snap, PERF_WINDOW_OVERLAY);

	if (snap.time > s_overlay_last.time)
		VideoDOS_FormatOverlay(&s_overlay_last, &snap);
	s_overlay_last = snap;

	VideoDOS_RenderOverlay();
	return true;
}

/* While waiting for the next tick: menus only redraw when something
 * changes, so put fresh numbers straight on the screen. */
static void
VideoDOS_IdleOverlay(void)
{
	static int l_shown_w, l_shown_h;

	if (s_overlay == OVERLAY_OFF || g_dos_backbuffer == NULL || !VideoDOS_UpdateOverlay() || s_overlay_bmp == NULL)
		return;

	const int y = (int)g_screenDiv[SCREENDIV_VIEWPORT].y;
	const int w = s_overlay_bmp->w;
	const int h = s_overlay_bmp->h;

	acquire_screen();
	/* Black out what is left of a wider or taller box. */
	if (l_shown_w > w)
		rectfill(screen, w, y, l_shown_w - 1, y + max(h, l_shown_h) - 1, 0);
	if (l_shown_h > h)
		rectfill(screen, 0, y + h, w - 1, y + l_shown_h - 1, 0);
	blit(s_overlay_bmp, screen, 0, 0, 0, y, w, h);
	release_screen();

	/* Keep the changed-span present in step with the screen. */
	if (s_front != NULL) {
		if (l_shown_w > w)
			rectfill(s_front, w, y, l_shown_w - 1, y + max(h, l_shown_h) - 1, 0);
		if (l_shown_h > h)
			rectfill(s_front, 0, y + h, w - 1, y + l_shown_h - 1, 0);
		blit(s_overlay_bmp, s_front, 0, 0, 0, y, w, h);
	}

	l_shown_w = w;
	l_shown_h = h;

	VideoDOS_UpdateCursor(true);
}

/* A solid box, copied each frame: cheaper than shading and drawing
 * glyphs every frame, which cost a DX4/100 about 2 ms. */
static void
VideoDOS_DrawOverlay(void)
{
	VideoDOS_UpdateOverlay();

	if (s_overlay_bmp == NULL)
		return;

	const int y = (int)g_screenDiv[SCREENDIV_VIEWPORT].y;
	blit(s_overlay_bmp, g_dos_backbuffer, 0, 0, 0, y, s_overlay_bmp->w, s_overlay_bmp->h);
}

/* Frames, worst frame and time per part since the snapshot, to DUNEDYN.LOG. */
static void
VideoDOS_LogPerformanceSince(const char *title, const PerfSnapshot *a, enum PerfWindow window)
{
	if (g_dos_backbuffer == NULL)
		return;

	PerfSnapshot b;
	Perf_Snapshot(&b, window);

	/* Menus only redraw when something changes: frame rates there mean
	 * nothing, so only report spans with game play. */
	const uint32_t frames = b.frames - a->frames;
	const double secs = (double)(b.time - a->time) / PERF_TICKS_PER_SECOND;
	if (frames == 0 || secs <= 0.0 || b.game_ticks == a->game_ticks)
		return;

#define LOG_MS(c)   (Perf_TicksToMs(b.ticks[c] - a->ticks[c]) / frames)

	RunLog_Printf("%s: %lu frames in %.0f s, %.1f fps average, worst frame %.0f ms, %lu game ticks\n",
		title, (unsigned long)frames, secs, frames / secs, Perf_TicksToMs(b.worst_frame),
		(unsigned long)(b.game_ticks - a->game_ticks));
	RunLog_Printf("%s: ms/frame: game %.2f, gui %.2f, draw %.2f, clear %.2f, present %.2f (%.0f%% changed); music interrupts %.2f%%\n",
		title, LOG_MS(PERF_GAME_TICKS), LOG_MS(PERF_GUI_TICKS), LOG_MS(PERF_DRAW),
		LOG_MS(PERF_CLEAR), LOG_MS(PERF_PRESENT),
		100.0 * (b.present_bytes - a->present_bytes) / frames / ((double)g_dos_backbuffer->w * g_dos_backbuffer->h),
		100.0 * (b.ticks[PERF_IRQ_MUSIC] - a->ticks[PERF_IRQ_MUSIC]) / (double)(b.time - a->time));

#undef LOG_MS
}

/* Whole run so far. */
void
VideoDOS_LogPerformance(const char *title)
{
	static const PerfSnapshot zero;

	if (s_mission_running)
		VideoDOS_MissionEnd();

	VideoDOS_LogPerformanceSince(title, &zero, PERF_WINDOW_RUN);
}

void
VideoDOS_MissionStart(void)
{
	Perf_Snapshot(&s_mission_base, PERF_WINDOW_MISSION);
	Perf_ResetFrameTime();
	s_mission_running = true;
}

void
VideoDOS_MissionEnd(void)
{
	if (!s_mission_running)
		return;

	s_mission_running = false;
	VideoDOS_LogPerformanceSince("Mission", &s_mission_base, PERF_WINDOW_MISSION);
}

/**
 * Copy the back buffer to video memory.  With a front copy, compare
 * each scanline in 32-bit words and send only the changed span.
 */
static uint32_t
VideoDOS_Present(void)
{
	BITMAP *bb = g_dos_backbuffer;
	uint32_t bytes = 0;

	acquire_screen();

	if (s_front == NULL) {
		blit(bb, screen, 0, 0, 0, 0, bb->w, bb->h);
		bytes = bb->w * bb->h;
	} else {
		const int words = bb->w / 4;

		for (int y = 0; y < bb->h; y++) {
			const uint32 *b = (const uint32 *)bb->line[y];
			uint32 *f = (uint32 *)s_front->line[y];
			int first = 0;
			int last = words - 1;

			while (first < words && b[first] == f[first])
				first++;

			if (first == words)
				continue;

			while (b[last] == f[last])
				last--;

			const int x = 4 * first;
			const int w = 4 * (last - first + 1);

			memcpy(&f[first], &b[first], w);
			blit(bb, screen, x, y, x, y, w, 1);
			bytes += w;
		}
	}

	release_screen();
	return bytes;
}

static void
VideoDOS_UpdateHardwarePalette(void)
{
	const RGB *want = s_frame_uses_bene ? s_bene_palette : s_game_palette;

	if (memcmp(want, s_hw_palette, sizeof(PALETTE)) != 0) {
		memcpy(s_hw_palette, want, sizeof(PALETTE));
		set_palette_range(s_hw_palette, 0, 255, FALSE);
	}
}

void
VideoA5_Tick(void)
{
	/* A frame that drew nothing still shows a cleared screen. */
	VideoDOS_TouchBackbuffer();

	const enum ScreenDivID div = A5_SaveTransform();
	A5_UseTransform(SCREENDIV_MAIN);

	if (s_overlay != OVERLAY_OFF)
		VideoDOS_DrawOverlay();

	/* Software mouse cursor, double sized on 640x400 and above.  With a
	 * front copy it goes straight to the screen after the present. */
	if (!g_mouseHidden && s_front == NULL) {
		const int size = (TRUE_DISPLAY_WIDTH >= 640) ? 32 : 16;
		/* Draw at the hardware mouse position in screen pixels.  The
		 * game's g_mouseX/Y are in screen coordinates in game but menu
		 * coordinates while an overlay (hints, options) is open, so
		 * converting those back through the current transform put the
		 * cursor in the wrong place when a hint popped up.
		 * Allegro 5 normally uses a hardware cursor, hiding this. */
		int mx, my;

		if (g_mousePanning) {
			/* The hardware mouse is re-centred while panning; the
			 * game keeps the cursor where panning started. */
			mx = g_mouseX;
			my = g_mouseY;
		} else {
			InputDOS_GetCursorPosition(&mx, &my);
		}

		const int x = mx - cursor_focus[g_cursorSpriteID].x * size / 16;
		const int y = my - cursor_focus[g_cursorSpriteID].y * size / 16;

		VideoDOS_Blit(s_shape[g_cursorSpriteID][HOUSE_HARKONNEN], 0, 0, 16, 16, x, y, size, size, 0, BLIT_MASKED, NULL, 0);
	}

	A5_UseTransform(div);

	VideoDOS_UpdateHardwarePalette();
	s_frame_uses_bene = false;

	const uint64_t t0 = Perf_Now();
	const uint32_t bytes = VideoDOS_Present();
	VideoDOS_UpdateCursor(true);
	Perf_Add(PERF_PRESENT, (uint32_t)(Perf_Now() - t0));
	Perf_AddFrame(bytes);

	s_frame++;

	if (s_frame == 1)
		s_first_frame_tick = Timer_GetTicks();

	VideoDOS_PollAutomation();

	/* As Allegro 5: every frame is drawn from scratch. */
	const uint64_t t1 = Perf_Now();
	set_clip_rect(g_dos_backbuffer, 0, 0, g_dos_backbuffer->w - 1, g_dos_backbuffer->h - 1);
	if (s_frame_ingame) {
		s_pending_clear = true;
	} else {
		clear_bitmap(g_dos_backbuffer);
	}
	s_frame_ingame = false;
	s_clear_skipped = false;
	VideoDOS_ApplyClip();
	Perf_Add(PERF_CLEAR, (uint32_t)(Perf_Now() - t1));
}

/*--------------------------------------------------------------*/

void
Video_SetPalette(const uint8 *palette, int from, int length)
{
	const uint8 *p = palette;
	assert(from + length <= 256);

	/* Dune II palettes are 6-bit VGA DAC values, as are Allegro 4's.
	 * As in the Allegro 5 backend, palette points at entry 'from'. */
	for (int i = from; i < from + length; i++) {
		s_game_palette[i].r = (*p++) & 0x3F;
		s_game_palette[i].g = (*p++) & 0x3F;
		s_game_palette[i].b = (*p++) & 0x3F;
	}
}

void
Video_SetClippingArea(int x, int y, int w, int h)
{
	/* As al_set_clipping_rectangle on the current div's sub-bitmap:
	 * untransformed pixels relative to its origin, within its bounds. */
	const int ox = s_div_bitmap[s_div].x;
	const int oy = s_div_bitmap[s_div].y;

	s_div_bitmap[s_div].cx1 = max(ox + x, ox);
	s_div_bitmap[s_div].cy1 = max(oy + y, oy);
	s_div_bitmap[s_div].cx2 = min(ox + x + w, ox + s_div_bitmap[s_div].w);
	s_div_bitmap[s_div].cy2 = min(oy + y + h, oy + s_div_bitmap[s_div].h);

	VideoDOS_ApplyClip();
}

void
Video_SetCursor(int spriteID)
{
	assert(spriteID < CURSOR_MAX);

	g_cursorSpriteID = spriteID;
}

void
Video_ShowCursor(void)
{
	g_mouseHidden = false;
}

void
Video_HideCursor(void)
{
	g_mouseHidden = true;
}

void
Video_HideHWCursor(void)
{
}

void
Video_WarpCursor(int x, int y)
{
	position_mouse(x, y);
}

void
Video_GrabCursor(void)
{
}

void
Video_UngrabCursor(void)
{
}

void
Video_ShadeScreen(int alpha)
{
	VideoDOS_TouchBackbuffer();
	const unsigned char *lut = VideoDOS_ShadeTable(clamp(0x00, alpha, 0xFF));
	BITMAP *bb = g_dos_backbuffer;

	for (int y = bb->ct; y < bb->cb; y++) {
		unsigned char *d = bb->line[y];

		for (int x = bb->cl; x < bb->cr; x++)
			d[x] = lut[d[x]];
	}
}

void
Video_HoldBitmapDrawing(bool hold)
{
	VARIABLE_NOT_USED(hold);
}

/*--------------------------------------------------------------*/

static FadeInAux *
VideoDOS_InitFadeInSprite(BITMAP *src, bool masked, int x, int y, int w, int h, bool fade_in)
{
	FadeInAux *aux = &s_fadeInAux;
	assert(w < SCREEN_WIDTH);
	assert(h < SCREEN_HEIGHT);

	for (int i = 0; i < w; i++) {
		aux->cols[i] = i;
	}

	for (int i = 0; i < h; i++) {
		aux->rows[i] = i;
	}

	for (int i = 0; i < w; i++) {
		const int j = Random_Xorshift_Range(0, w - 1);
		const int swap = aux->cols[j];

		aux->cols[j] = aux->cols[i];
		aux->cols[i] = swap;
	}

	for (int i = 0; i < h; i++) {
		const int j = Random_Xorshift_Range(0, h - 1);
		const int swap = aux->rows[j];

		aux->rows[j] = aux->rows[i];
		aux->rows[i] = swap;
	}

	aux->fade_in = fade_in;
	aux->frame = 0;
	aux->bmp = src;
	aux->masked = masked;
	aux->x = x;
	aux->y = y;
	aux->width = w;
	aux->height = h;

	free(aux->revealed);
	aux->revealed = calloc(w * h, 1);

	return aux;
}

/* Dissolve: draw the revealed pixels (fade in) or the hidden ones (fade
 * out), as the Allegro 5 stencil versions. */
void
Video_DrawFadeIn(const FadeInAux *aux)
{
	if (aux == NULL || aux->bmp == NULL || aux->revealed == NULL)
		return;

	/* Full screen CPS sources are read at (x, y); shapes from (0, 0). */
	const bool full = (aux->bmp->w != aux->width) || (aux->bmp->h != aux->height);
	const int sx0 = full ? aux->x : 0;
	const int sy0 = full ? aux->y : 0;
	const enum BlitMode mode = aux->masked ? BLIT_MASKED : BLIT_OPAQUE;

	for (int y = 0; y < aux->height; y++) {
		const unsigned char *row = aux->revealed + aux->width * y;
		const unsigned char want = aux->fade_in ? 1 : 0;
		int x = 0;

		while (x < aux->width) {
			if (row[x] != want) {
				x++;
				continue;
			}

			int run = 1;
			while (x + run < aux->width && row[x + run] == want)
				run++;

			VideoDOS_Blit(aux->bmp, sx0 + x, sy0 + y, run, 1, aux->x + x, aux->y + y, run, 1, 0, mode, NULL, 0);
			x += run;
		}
	}
}

bool
Video_TickFadeIn(FadeInAux *aux)
{
	if (aux == NULL)
		return true;

	assert(0 <= aux->frame && aux->frame <= aux->height);
	if (aux->frame >= aux->height)
		return true;

	if (aux->revealed != NULL) {
		int j = aux->frame;
		for (int i = 0; i < aux->width; i++) {
			aux->revealed[aux->width * aux->rows[j] + aux->cols[i]] = 1;

			if (++j >= aux->height)
				j = 0;
		}
	}

	aux->frame++;
	return false;
}

/*--------------------------------------------------------------*/

static CPSStore *
VideoDOS_ExportCPS(enum SearchDirectory dir, const char *filename, unsigned char *buf)
{
	CPSStore *cps = malloc(sizeof(*cps));
	if (cps == NULL)
		return NULL;

	cps->next = NULL;
	if (dir == SEARCHDIR_CAMPAIGN_DIR) {
		snprintf(cps->filename, sizeof(cps->filename), "%s%s", g_campaign_list[g_campaign_selected].dir_name, filename);
	} else {
		snprintf(cps->filename, sizeof(cps->filename), "%s", filename);
	}

	cps->bmp = VideoDOS_CreateBitmap(SCREEN_WIDTH, SCREEN_HEIGHT);

	/* Mentat and misc screens use the Bene Gesserit palette. */
	cps->benepal = (strncmp(filename, "MENTATM.CPS", 11) == 0) || (strncmp(filename, "MISC", 4) == 0);

	memset(buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
	Sprites_LoadImage(dir, filename, SCREEN_1, NULL);
	VideoDOS_CopyBitmap(SCREEN_WIDTH, buf, cps->bmp, BLACK_COLOUR_0);

	return cps;
}

static CPSStore *
VideoDOS_LoadCPS(enum SearchDirectory dir, const char *filename)
{
	char campname[1024];
	CPSStore *cps;

	if (dir == SEARCHDIR_CAMPAIGN_DIR) {
		snprintf(campname, sizeof(campname), "%s%s", g_campaign_list[g_campaign_selected].dir_name, filename);
	} else {
		snprintf(campname, sizeof(campname), "%s", filename);
	}

	cps = s_cps;
	while (cps != NULL) {
		if (strncmp(cps->filename, campname, sizeof(cps->filename)) == 0)
			return cps;

		cps = cps->next;
	}

	cps = VideoDOS_ExportCPS(dir, filename, GFX_Screen_Get_ByIndex(SCREEN_1));
	if (cps == NULL)
		return NULL;

	cps->next = s_cps;
	s_cps = cps;

	return cps;
}

static void
VideoDOS_FreeCPS(CPSStore *cps)
{
	destroy_bitmap(cps->bmp);
	free(cps);
}

static void
VideoDOS_UseCPS(const CPSStore *cps)
{
	if (cps->benepal)
		s_frame_uses_bene = true;
}

/* As VideoA5_DrawBitmapRegion_Padded, without the padding: there is no
 * texture filtering to bleed across edges. */
static void
VideoDOS_CopyRegion(BITMAP *src, const struct CPSSpecialCoord *coord, int tx, int ty)
{
	blit(src, interface_texture, coord->cx, coord->cy, tx, ty, coord->w, coord->h);
}

static void
VideoDOS_InitCPS(void)
{
	const struct CPSSpecialCoord *coord;
	unsigned char *buf = GFX_Screen_Get_ByIndex(SCREEN_1);

	CPSStore *cps_screen = VideoDOS_ExportCPS(SEARCHDIR_GLOBAL_DATA_DIR, "SCREEN.CPS", buf);
	CPSStore *cps_fame = VideoDOS_LoadCPS(SEARCHDIR_GLOBAL_DATA_DIR, "FAME.CPS");
	CPSStore *cps_mapmach = VideoDOS_LoadCPS(SEARCHDIR_GLOBAL_DATA_DIR, "MAPMACH.CPS");
	assert(cps_screen != NULL && cps_fame != NULL && cps_mapmach != NULL);

	for (enum CPSID cpsID = CPS_MENUBAR_LEFT; cpsID <= CPS_STATUSBAR_RIGHT; cpsID++) {
		coord = &cps_special_coord[cpsID];

		if (coord->cx < 0 || coord->cy < 0)
			continue;

		VideoDOS_CopyRegion(cps_screen->bmp, coord, coord->tx, coord->ty);
	}

	coord = &cps_special_coord[CPS_STATUSBAR_MIDDLE];
	blit(cps_screen->bmp, interface_texture,  8, 17 - 1, coord->tx - 1, coord->ty - 1, 304, coord->h + 1);
	blit(cps_screen->bmp, interface_texture, 55, 17 - 1, coord->tx + 303, coord->ty - 1, 123, coord->h + 1);
	rectfill(interface_texture, coord->tx - 1, coord->ty + coord->h, coord->tx + coord->w, coord->ty + coord->h, 12);

	coord = &cps_special_coord[CPS_STATUSBAR_RIGHT];
	rectfill(interface_texture, coord->tx - 1, coord->ty + coord->h, coord->tx + coord->w, coord->ty + coord->h, 12);

	for (enum HouseType houseID = HOUSE_HARKONNEN; houseID < HOUSE_NEUTRAL; houseID++) {
		Sprites_LoadImage(SEARCHDIR_GLOBAL_DATA_DIR, "SCREEN.CPS", SCREEN_1, NULL);
		GUI_Palette_CreateRemap(houseID);
		GUI_Palette_RemapScreen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_1, g_remap);
		VideoDOS_CopyBitmap(SCREEN_WIDTH, buf, cps_screen->bmp, TRANSPARENT_COLOUR_0);

		coord = &cps_special_coord[CPS_SIDEBAR_TOP];
		VideoDOS_CopyRegion(cps_screen->bmp, coord, coord->tx + 17 * houseID, coord->ty + 4 * houseID);

		coord = &cps_special_coord[CPS_SIDEBAR_MIDDLE];
		VideoDOS_CopyRegion(cps_screen->bmp, coord, coord->tx + 17 * houseID, coord->ty + 4 * houseID);

		coord = &cps_special_coord[CPS_SIDEBAR_BOTTOM];
		VideoDOS_CopyRegion(cps_screen->bmp, coord, coord->tx + 17 * houseID, coord->ty + 23 * houseID);
	}

	coord = &cps_special_coord[CPS_MENUBAR_MIDDLE];
	blit(cps_fame->bmp, interface_texture, 134, 37, coord->tx - 1, coord->ty - 1, 111, coord->h + 2);
	blit(cps_mapmach->bmp, interface_texture, 55, 183, coord->tx + 110, coord->ty - 1, 211, coord->h);
	for (int i = 0; i < 2; i++) {
		/* ALLEGRO_FLIP_VERTICAL of a 2 pixel high strip. */
		blit(cps_mapmach->bmp, interface_texture, 55, 198 + 1 - i, coord->tx + 110, coord->ty - 1 + coord->h + i, 211, 1);
	}

	coord = &cps_special_coord[CPS_CONQUEST_EN];
	assert(coord->tx == cps_special_coord[CPS_CONQUEST_FR].tx);
	assert(coord->tx == cps_special_coord[CPS_CONQUEST_DE].tx);
	for (enum CPSID cpsID = CPS_CONQUEST_EN; cpsID <= CPS_CONQUEST_DE; cpsID++) {
		VideoDOS_CopyRegion(cps_mapmach->bmp, &cps_special_coord[cpsID], coord->tx, cps_special_coord[cpsID].ty);
	}

	/* White masks of the conquest text, tinted by house when drawn. */
	Sprites_LoadImage(SEARCHDIR_GLOBAL_DATA_DIR, "MAPMACH.CPS", SCREEN_1, NULL);
	for (enum CPSID cpsID = CPS_CONQUEST_EN; cpsID <= CPS_CONQUEST_DE; cpsID++) {
		const struct CPSSpecialCoord *c = &cps_special_coord[cpsID];

		for (int y = 0; y < 20; y++) {
			for (int x = 0; x < coord->w; x++) {
				const unsigned char v = buf[SCREEN_WIDTH * (c->cy + y) + (coord->cx + x)];
				interface_texture->line[c->ty + 30 + y][coord->tx + x] = (v == CONQUEST_COLOUR) ? 0xFF : 0x00;
			}
		}
	}

	VideoDOS_FreeCPS(cps_screen);
}

void
VideoA5_DrawCPS(enum SearchDirectory dir, const char *filename)
{
	CPSStore *cps = VideoDOS_LoadCPS(dir, filename);

	if (cps != NULL) {
		VideoDOS_UseCPS(cps);
		VideoDOS_Draw(cps->bmp, 0, 0, 0, BLIT_OPAQUE, NULL, 0);
	}
}

void
VideoA5_DrawCPSCoordinates(enum SearchDirectory dir, const char *filename, int dx, int dy)
{
	CPSStore *cps = VideoDOS_LoadCPS(dir, filename);

	if (cps != NULL) {
		VideoDOS_UseCPS(cps);
		VideoDOS_Draw(cps->bmp, dx, dy, 0, BLIT_OPAQUE, NULL, 0);
	}
}

void
VideoA5_DrawCPSRegion(enum SearchDirectory dir, const char *filename, int sx, int sy, int dx, int dy, int w, int h)
{
	CPSStore *cps = VideoDOS_LoadCPS(dir, filename);

	if (cps != NULL) {
		VideoDOS_UseCPS(cps);
		VideoDOS_Blit(cps->bmp, sx, sy, w, h, dx, dy, w, h, 0, BLIT_OPAQUE, NULL, 0);
	}
}

void
VideoA5_DrawCPSSpecial(enum CPSID cpsID, enum HouseType houseID, int x, int y)
{
	const unsigned char tint[HOUSE_NEUTRAL][3] = {
		{ 0x98, 0x00, 0x00 }, { 0x28, 0x3C, 0x98 }, { 0x24, 0x98, 0x24 }, { 0x98, 0x4C, 0x04 }, { 0xA8, 0x30, 0xA8 }, { 0x98, 0x68, 0x00 }
	};
	assert(cpsID < CPS_SPECIAL_MAX);
	assert(houseID < HOUSE_NEUTRAL);

	const struct CPSSpecialCoord *coord = &cps_special_coord[cpsID];

	int sx = coord->tx;
	int sy = coord->ty;

	if (CPS_CONQUEST_EN <= cpsID && cpsID <= CPS_CONQUEST_DE) {
		const int col = VideoDOS_NearestColour(tint[houseID][0], tint[houseID][1], tint[houseID][2]);

		VideoDOS_Blit(interface_texture, sx, sy, coord->w, coord->h, x, y, coord->w, coord->h, 0, BLIT_MASKED, NULL, 0);
		VideoDOS_Blit(interface_texture, sx, sy + 30, coord->w, 20, x, y, coord->w, 20, 0, BLIT_SOLID, NULL, col);
		return;
	}

	if (CPS_SIDEBAR_TOP <= cpsID && cpsID <= CPS_SIDEBAR_BOTTOM) {
		sx += 17 * houseID;

		if (cpsID == CPS_SIDEBAR_BOTTOM) {
			sy += 23 * houseID;
		} else {
			sy += 4 * houseID;
		}
	}

	VideoDOS_Blit(interface_texture, sx, sy, coord->w, coord->h, x, y, coord->w, coord->h, 0, BLIT_MASKED, NULL, 0);
}

void
VideoA5_DrawCPSSpecialScale(enum CPSID cpsID, enum HouseType houseID, int x, int y, float scale)
{
	/* This is only used to draw interface when rendering the blur brush. */
	assert(CPS_SIDEBAR_TOP <= cpsID && cpsID <= CPS_SIDEBAR_BOTTOM);
	assert(houseID < HOUSE_NEUTRAL);

	const struct CPSSpecialCoord *coord = &cps_special_coord[cpsID];
	int sx = coord->tx + 17 * houseID;
	int sy = coord->ty;

	if (cpsID == CPS_SIDEBAR_BOTTOM) {
		sy += 23 * houseID;
	} else {
		sy += 4 * houseID;
	}

	VideoDOS_Blit(interface_texture, sx, sy, coord->w, coord->h,
			x, y, scale * coord->w, scale * coord->h, 0, BLIT_MASKED, NULL, 0);
}

FadeInAux *
Video_InitFadeInCPS(const char *filename, int x, int y, int w, int h, bool fade_in)
{
	CPSStore *cps = VideoDOS_LoadCPS(SEARCHDIR_GLOBAL_DATA_DIR, filename);
	if (cps == NULL)
		return NULL;

	return VideoDOS_InitFadeInSprite(cps->bmp, false, x, y, w, h, fade_in);
}

/*--------------------------------------------------------------*/

static int
VideoDOS_NumIconsInGroup(enum IconMapEntries group)
{
	if (!(0 < group && group < ICM_ICONGROUP_EOF))
		return 0;

	/* group == ICM_ICONGROUP_RADAR_OUTPOST. */
	if (group + 1 == ICM_ICONGROUP_EOF) {
		return 24;
	} else {
		return g_iconMap[group + 1] - g_iconMap[group];
	}
}

static void
VideoDOS_ExportIconGroup(enum IconMapEntries group, int num_common,
		int x, int y, int *retx, int *rety)
{
	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	const int WINDOW_H = g_widgetProperties[WINDOWID_RENDER_TEXTURE].height;
	const int num = VideoDOS_NumIconsInGroup(group);

	if (num_common < 0)
		num_common = num;

	for (enum HouseType houseID = HOUSE_HARKONNEN; houseID < HOUSE_NEUTRAL; houseID++) {
		GUI_Palette_CreateRemap(houseID);

		for (int idx = 0; idx < num; idx++) {
			const uint16 iconID = g_iconMap[g_iconMap[group] + idx];
			assert(iconID < ICONID_MAX);

			if (s_icon[iconID][houseID].sx != 0 || s_icon[iconID][houseID].sy != 0)
				continue;

			if ((idx >= num_common) || (houseID == HOUSE_HARKONNEN)) {
				VideoDOS_GetNextXY(WINDOW_W, WINDOW_H, x, y, TILE_SIZE + 1, TILE_SIZE + 1, TILE_SIZE + 1, &x, &y);

				GFX_DrawSprite_(iconID, x, y, houseID);

				s_icon[iconID][houseID].sx = x;
				s_icon[iconID][houseID].sy = y;
				x += TILE_SIZE + 2;
			} else {
				s_icon[iconID][houseID] = s_icon[iconID][HOUSE_HARKONNEN];
			}
		}
	}

	*retx = 1;
	*rety = y + TILE_SIZE + 2;
}

static void
VideoDOS_InitIcons(unsigned char *buf)
{
	const struct {
		int num_common;
		enum IconMapEntries group;
	} icon_data[] = {
		{ -1, ICM_ICONGROUP_ROCK_CRATERS },
		{ -1, ICM_ICONGROUP_SAND_CRATERS },
		{ -1, ICM_ICONGROUP_FLY_MACHINES_CRASH },
		{ -1, ICM_ICONGROUP_SAND_TRACKS },
		{ -1, ICM_ICONGROUP_WALLS },
		{ -1, ICM_ICONGROUP_FOG_OF_WAR },
		{ -1, ICM_ICONGROUP_CONCRETE_SLAB },
		{ -1, ICM_ICONGROUP_LANDSCAPE },
		{ -1, ICM_ICONGROUP_SPICE_BLOOM },
		{ -1, ICM_ICONGROUP_BASE_DEFENSE_TURRET },
		{ -1, ICM_ICONGROUP_BASE_ROCKET_TURRET },

		{  0, ICM_ICONGROUP_SAND_DEAD_BODIES },
		{ 18, ICM_ICONGROUP_HOUSE_PALACE },
		{  8, ICM_ICONGROUP_LIGHT_VEHICLE_FACTORY },
		{ 12, ICM_ICONGROUP_HEAVY_VEHICLE_FACTORY },
		{ 12, ICM_ICONGROUP_HI_TECH_FACTORY },
		{  8, ICM_ICONGROUP_IX_RESEARCH },
		{  8, ICM_ICONGROUP_WOR_TROOPER_FACILITY },
		{  8, ICM_ICONGROUP_CONSTRUCTION_YARD },
		{  8, ICM_ICONGROUP_INFANTRY_BARRACKS },
		{  8, ICM_ICONGROUP_WINDTRAP_POWER },
		{ 18, ICM_ICONGROUP_STARPORT_FACILITY },
		{ 12, ICM_ICONGROUP_SPICE_REFINERY },
		{ 12, ICM_ICONGROUP_VEHICLE_REPAIR_CENTRE },
		{  8, ICM_ICONGROUP_SPICE_STORAGE_SILO },
		{  8, ICM_ICONGROUP_RADAR_OUTPOST },

		{  0, ICM_ICONGROUP_EOF }
	};

	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	int x = 1, y = 1;

	for (int i = 0; icon_data[i].group < ICM_ICONGROUP_EOF; i++) {
		VideoDOS_ExportIconGroup(icon_data[i].group, icon_data[i].num_common, x, y, &x, &y);
	}

	/* Windtraps keep WINDTRAP_COLOUR, which the game animates in the
	 * palette, so no overlay textures are needed (unlike Allegro 5). */

	icon_texture = VideoDOS_CreateBitmap(WINDOW_W, y + 1);
	VideoDOS_CopyBitmap(WINDOW_W, buf, icon_texture, TRANSPARENT_COLOUR_0);

	for (int iconID = 0; iconID < ICONID_MAX; iconID++) {
		for (enum HouseType houseID = HOUSE_HARKONNEN; houseID < HOUSE_NEUTRAL; houseID++) {
			const IconCoord *coord = &s_icon[iconID][houseID];
			if (coord->sx == 0 && coord->sy == 0)
				continue;

			const unsigned char first = icon_texture->line[coord->sy][coord->sx];
			bool has_zero = false;
			bool solid = true;

			for (int j = 0; j < TILE_SIZE; j++) {
				const unsigned char *row = icon_texture->line[coord->sy + j] + coord->sx;

				for (int i = 0; i < TILE_SIZE; i++) {
					if (row[i] == 0)
						has_zero = true;
					if (row[i] != first)
						solid = false;
				}
			}

			s_icon_kind[iconID][houseID] = has_zero ? ICON_MASKED : (solid ? ICON_SOLID : ICON_OPAQUE);
			s_icon_colour[iconID][houseID] = first;
		}
	}
}

/* Fill a rectangle in the current ScreenDiv with one colour. */
static void
VideoDOS_FillRect(int x, int y, int w, int h, int c)
{
	BITMAP *dst = g_dos_backbuffer;
	int x1, y1, x2, y2;

	VideoDOS_ToScreen(x, y, &x1, &y1);
	VideoDOS_ToScreen(x + w, y + h, &x2, &y2);

	x1 = max(x1, dst->cl);
	y1 = max(y1, dst->ct);
	x2 = min(x2, dst->cr);
	y2 = min(y2, dst->cb);

	if (x1 >= x2)
		return;

	const uint32 pattern = 0x01010101u * (unsigned char)c;

	for (int j = y1; j < y2; j++) {
		unsigned char *d = dst->line[j] + x1;
		int n = x2 - x1;

		/* Short rows: memset's call overhead dominates, so store
		 * aligned 32-bit words inline. */
		while (n > 0 && ((uintptr_t)d & 3) != 0) {
			*d++ = c;
			n--;
		}

		for (; n >= 4; n -= 4, d += 4)
			*(uint32 *)d = pattern;

		while (n-- > 0)
			*d++ = c;
	}
}

void
VideoA5_DrawIcon(uint16 iconID, enum HouseType houseID, int x, int y)
{
	assert(iconID < ICONID_MAX);
	assert(houseID < HOUSE_NEUTRAL);

	const IconCoord *coord = &s_icon[iconID][houseID];
	assert(coord->sx != 0 && coord->sy != 0);

	switch (s_icon_kind[iconID][houseID]) {
		case ICON_SOLID:
			/* Solid icons (shroud) never contain colour 0, so a fill
			 * through the opaque path is equivalent.
			 *
			 * Viewport tiles are the first thing drawn after the back
			 * buffer is cleared to colour 0, so a solid tile that looks
			 * the same as colour 0 (black shroud) need not be drawn.
			 * Skipping it saves most of a 486's frame on unexplored maps. */
			if (s_div == SCREENDIV_VIEWPORT) {
				const RGB *p = &s_game_palette[s_icon_colour[iconID][houseID]];
				const RGB *p0 = &s_game_palette[0];

				if (p->r == p0->r && p->g == p0->g && p->b == p0->b)
					break;
			}

			{
				const uint64_t t0 = s_profiling ? Perf_Now() : 0;
				VideoDOS_FillRect(x, y, TILE_SIZE, TILE_SIZE, s_icon_colour[iconID][houseID]);
				if (s_profiling)
					s_fill_ticks += Perf_Now() - t0;
			}
			break;

		case ICON_OPAQUE:
			VideoDOS_BlitAt(icon_texture, coord->sx, coord->sy, TILE_SIZE, TILE_SIZE, x, y, 0, BLIT_OPAQUE, NULL, 0);
			break;

		default:
			VideoDOS_BlitAt(icon_texture, coord->sx, coord->sy, TILE_SIZE, TILE_SIZE, x, y, 0, BLIT_MASKED, NULL, 0);
			break;
	}
}

void
VideoA5_DrawIconAlpha(uint16 iconID, int x, int y, unsigned char alpha)
{
	assert(iconID < ICONID_MAX);

	const IconCoord *coord = &s_icon[iconID][HOUSE_HARKONNEN];
	assert(coord->sx != 0 && coord->sy != 0);

	/* Black icon at alpha (fog of war): darken what is underneath. */
	VideoDOS_BlitAt(icon_texture, coord->sx, coord->sy, TILE_SIZE, TILE_SIZE, x, y,
			0, BLIT_SHADE, VideoDOS_ShadeTable(alpha), 0);
}

void
VideoA5_DrawRectCross(int x1, int y1, int w, int h, unsigned char c)
{
	const int x2 = x1 + (w * TILE_SIZE) - 1;
	const int y2 = y1 + (h * TILE_SIZE) - 1;

	Prim_Rect_i(x1, y1, x2, y2, c);
	Prim_Line(x1 + 0.33f, y1 + 0.33f, x2 + 0.66f, y2 + 0.66f, c, 0.75f);
	Prim_Line(x2 + 0.66f, y1 + 0.33f, x1 + 0.33f, y2 + 0.66f, c, 0.75f);
}

/*--------------------------------------------------------------*/

/* Draw a shape into the SCREEN_0 export buffer and cut a sub bitmap of
 * the texture at the same place, as VideoA5_ExportShape. */
static BITMAP *
VideoDOS_ExportShape(BITMAP *texture, enum ShapeID shapeID, int x, int y, int row_h,
		int *retx, int *rety, int *ret_row_h, unsigned char *remap)
{
	const int w = Shape_Width(shapeID);
	const int h = Shape_Height(shapeID);

	VideoDOS_GetNextXY(texture->w, texture->h, x, y, w, h, row_h, &x, &y);
	GUI_DrawSprite_(SCREEN_0, g_sprites[shapeID], x, y, WINDOWID_RENDER_TEXTURE, 0x100, remap, 1);

	BITMAP *bmp = VideoDOS_SubBitmap(texture, x, y, w, h);
	assert(bmp != NULL);

	*retx = x + w + 1;
	*rety = y;
	*ret_row_h = (x <= 1) ? h : max(row_h, h);
	return bmp;
}

static BITMAP *
VideoDOS_ExportCheckBox(BITMAP *texture, unsigned char *buf, bool checked, int x, int y, int row_h, int *retx, int *rety, int *ret_row_h)
{
	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	const int w = 9;
	const int h = 9;
	const uint8 colour = 31;

	VideoDOS_GetNextXY(texture->w, texture->h, x, y, w, h, row_h, &x, &y);

	/* Draw into the export buffer, which is copied to the texture later. */
	for (int i = 0; i < 9; i++) {
		buf[WINDOW_W * y + x + i] = colour;
		buf[WINDOW_W * (y + 8) + x + i] = colour;
		buf[WINDOW_W * (y + i) + x] = colour;
		buf[WINDOW_W * (y + i) + x + 8] = colour;
	}

	if (checked) {
		for (int i = 2; i <= 6; i++) {
			buf[WINDOW_W * (y + i) + x + i] = colour;
			buf[WINDOW_W * (y + i) + x + 8 - i] = colour;
		}
	}

	BITMAP *bmp = VideoDOS_SubBitmap(texture, x, y, w, h);
	assert(bmp != NULL);

	*retx = x + w + 1;
	*rety = y;
	*ret_row_h = (x <= 1) ? h : max(row_h, h);
	return bmp;
}

static void
VideoDOS_InitShapeCHOAMButtons(unsigned char *buf, int y1)
{
	const struct {
		int sx, dx;
		size_t w;
	} cmd[] = {
		{ 128 + 96, 128 + 90, 15 }, /* S  from BUILD THIS */
		{ 128 + 23, 128 + 80, 10 }, /* U  from BUILD THIS */
		{ 352 + 16, 128 + 21, 10 }, /* P  from OPTIONS. */
		{ 544 + 66, 128 + 31, 16 }, /* R  from SEND ORDER. */
		{ 448 + 67, 128 + 47,  6 }, /* E  from INVOICE */
		{ 448 + 28, 128 + 53,  9 }, /* V  from INVOICE */
		{ 448 +  7, 128 + 62,  8 }, /* I  from INVOICE */
		{ 352 + 42, 128 + 70,  4 }, /* IO from OPTIONS */
		{ 448 + 41, 128 + 74,  6 }, /* O  from INVOICE */

		{ 128 +100, 128 + 12,  9 }, /* Fill left of PREVIOUS */
	};

	const int y2 = y1 + 17;
	const int h = 11;

	/* Create a "previous" button from other CHOAM buttons.
	 * The invoice button has 6 pixels of blank to the left.
	 * Avoid using French buttons.
	 */
	Sprites_InitCHOAM("BTTN.ENG", "CHOAM.ENG");
	GUI_DrawSprite_(0, g_sprites[SHAPE_RESUME_GAME + 0], 4, y1, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_RESUME_GAME + 1], 4, y2, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_BUILD_THIS + 0], 128, y1, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_BUILD_THIS + 1], 128, y2, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_MENTAT + 0], 256, y1, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_MENTAT + 1], 256, y2, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_OPTIONS + 0], 352, y1, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_OPTIONS + 1], 352, y2, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_INVOICE + 0], 448 - 6, y1, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_INVOICE + 1], 448 - 6, y2, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_SEND_ORDER + 0], 544, y1, WINDOWID_RENDER_TEXTURE, 0);
	GUI_DrawSprite_(0, g_sprites[SHAPE_SEND_ORDER + 1], 544, y2, WINDOWID_RENDER_TEXTURE, 0);

	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	unsigned char *row1 = &buf[WINDOW_W * (y1 + 2)];
	unsigned char *row2 = &buf[WINDOW_W * (y2 + 2)];

	for (int i = 0; i < h; i++) {
		for (unsigned int c = 0; c < lengthof(cmd); c++) {
			memmove(row1 + cmd[c].dx, row1 + cmd[c].sx, cmd[c].w);
			memmove(row2 + cmd[c].dx, row2 + cmd[c].sx, cmd[c].w);
		}

		row1 += WINDOW_W;
		row2 += WINDOW_W;
	}

	s_shape[SHAPE_RESUME_GAME + 0][HOUSE_HARKONNEN] = VideoDOS_SubBitmap(shape_texture, 128, y1, 120, 16);
	s_shape[SHAPE_RESUME_GAME + 1][HOUSE_HARKONNEN] = VideoDOS_SubBitmap(shape_texture, 128, y2, 120, 16);
	assert(s_shape[SHAPE_RESUME_GAME + 0][HOUSE_HARKONNEN] != NULL);
	assert(s_shape[SHAPE_RESUME_GAME + 1][HOUSE_HARKONNEN] != NULL);

	for (enum HouseType houseID = HOUSE_HARKONNEN + 1; houseID < HOUSE_NEUTRAL; houseID++) {
		s_shape[SHAPE_RESUME_GAME + 0][houseID] = s_shape[SHAPE_RESUME_GAME + 0][HOUSE_HARKONNEN];
		s_shape[SHAPE_RESUME_GAME + 1][houseID] = s_shape[SHAPE_RESUME_GAME + 1][HOUSE_HARKONNEN];
	}
}

static void
VideoDOS_InitShapes(unsigned char *buf)
{
	/* Check Sprites_Init. */
	const struct {
		int start, end;
		bool remap;
	} shape_data[] = {
		{   0,   6, false }, /* MOUSE.SHP */
		{  12, 110, false }, /* SHAPES.SHP */
		{   7,  11,  true }, /* BTTN */
		{ 111, 140,  true }, /* UNITS2.SHP */
		{ 141, 150, false }, /* UNITS2.SHP: sonic tank turret, launcher turret */
		{ 151, 161, false }, /* UNITS1.SHP */
		{ 162, 167,  true }, /* UNITS1.SHP: tanks */
		{ 168, 207, false }, /* UNITS1.SHP */
		{ 208, 212,  true }, /* UNITS1.SHP: deviator gas */
		{ 213, 237, false }, /* UNITS1.SHP */
		{ 238, 257,  true }, /* UNITS.SHP: quad .. mcv */
		{ 258, 282, false }, /* UNITS.SHP: rockets */
		{ 283, 300,  true }, /* UNITS.SHP: carryall .. frigate */
		{ 301, 354,  true }, /* UNITS.SHP: saboteur .. landed ornithoper */
		{ 373, 386, false }, /* MENTAT */
		{ 525, 526, false }, /* SHAPE_CHECKBOX_OFF and SHAPE_CHECKBOX_ON */

		{  -2,   0, false },
		{ 477, 504,  true }, /* PIECES.SHP */
		{ 505, 513, false }, /* ARROWS.SHP */

		{  -1,   0, false }
	};

	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	const int WINDOW_H = g_widgetProperties[WINDOWID_RENDER_TEXTURE].height;

	BITMAP *texture = shape_texture;
	int x = 0, y = 0, row_h = 0;
	unsigned char greymap[256];

	uint8 fileID = File_Open("GRAYRMAP.TBL", FILE_MODE_READ);
	assert(fileID != FILE_INVALID);

	File_Read(fileID, greymap, 256);
	File_Close(fileID);

	/* Use colour 12 for black, colour 0 is transparent. */
	for (int i = 0; i < 256; i++) {
		if (greymap[i] == 0)
			greymap[i] = 12;
	}

	for (int group = 0; shape_data[group].start != -1; group++) {
		if (shape_data[group].start == -2) {
			VideoDOS_InitShapeCHOAMButtons(buf, y + row_h + 1);
			VideoDOS_CopyBitmap(WINDOW_W, buf, shape_texture, SKIP_COLOUR_0);
			memset(buf, 0, WINDOW_W * WINDOW_H);

			texture = region_texture;
			x = 0, y = 0, row_h = 0;
			continue;
		}

		for (enum HouseType houseID = HOUSE_HARKONNEN; houseID < HOUSE_NEUTRAL; houseID++) {
			if (shape_data[group].start == SHAPE_DEVIATOR_GAS_CLOUD) {
				GUI_Palette_CreateRemapDeviatorGas(houseID);
			} else {
				GUI_Palette_CreateRemap(houseID);
			}

			for (uint16 shapeID = shape_data[group].start; shapeID <= shape_data[group].end; shapeID++) {
				assert(shapeID < SHAPEID_MAX);

				if ((shape_data[group].remap) || (houseID == HOUSE_HARKONNEN)) {
					if (shapeID == SHAPE_RADIO_BUTTON_OFF || shapeID == SHAPE_RADIO_BUTTON_ON) {
						const uint8 backup = g_remap[RADIO_BUTTON_BACKGROUND_COLOUR];
						g_remap[RADIO_BUTTON_BACKGROUND_COLOUR] = 0;

						s_shape[shapeID][houseID] = VideoDOS_ExportShape(texture, shapeID, x, y, row_h, &x, &y, &row_h, g_remap);

						g_remap[RADIO_BUTTON_BACKGROUND_COLOUR] = backup;
					} else if ((shapeID == SHAPE_CHECKBOX_OFF) || (shapeID == SHAPE_CHECKBOX_ON)) {
						const bool checked = (shapeID == SHAPE_CHECKBOX_ON);
						s_shape[shapeID][houseID] = VideoDOS_ExportCheckBox(texture, buf, checked, x, y, row_h, &x, &y, &row_h);
					} else {
						s_shape[shapeID][houseID] = VideoDOS_ExportShape(texture, shapeID, x, y, row_h, &x, &y, &row_h, g_remap);
					}

					if (SHAPE_CONCRETE_SLAB <= shapeID && shapeID <= SHAPE_SANDWORM) {
						const enum ShapeID greyID = SHAPE_CONCRETE_SLAB_GREY + (shapeID - SHAPE_CONCRETE_SLAB);
						s_shape[greyID][houseID] = VideoDOS_ExportShape(texture, shapeID, x, y, row_h, &x, &y, &row_h, greymap);
					}
				} else {
					s_shape[shapeID][houseID] = s_shape[shapeID][HOUSE_HARKONNEN];
				}
			}
		}
	}

	for (int shapeID = SHAPE_ARROW; shapeID <= SHAPE_ARROW_FINAL; shapeID++) {
		const int tintID = SHAPE_ARROW_TINT + 5 * (shapeID - SHAPE_ARROW);
		const int w = Shape_Width(shapeID);
		const int h = Shape_Height(shapeID);

		VideoDOS_GetNextXY(WINDOW_W, WINDOW_H, x, y, 5 * w, h, row_h, &x, &y);
		GUI_DrawSprite_(SCREEN_0, g_sprites[shapeID], x, y, WINDOWID_RENDER_TEXTURE, 0);

		for (int i = 4; i >= 0; i--) {
			const int c = (i == 0) ? STRATEGIC_MAP_ARROW_EDGE_COLOUR : (STRATEGIC_MAP_ARROW_COLOUR + i - 1);
			assert(s_shape[tintID + i][0] == NULL);

			s_shape[tintID + i][0] = VideoDOS_SubBitmap(region_texture, x + i * w, y, w, h);
			assert(s_shape[tintID + i][0] != NULL);

			VideoDOS_CreateWhiteMaskIndexed(buf, WINDOW_W, x, y, x + i * w, y, w, h, c);
		}

		x += 5 * w + 1;
		row_h = max(row_h, h);
	}

	VideoDOS_CopyBitmap(WINDOW_W, buf, region_texture, TRANSPARENT_COLOUR_0);
}

void
VideoA5_DrawShape(enum ShapeID shapeID, enum HouseType houseID, int x, int y, int flags)
{
	assert(shapeID < SHAPEID_MAX);
	assert(houseID < HOUSE_NEUTRAL);
	assert(s_shape[shapeID][houseID] != NULL);

	BITMAP *bmp = s_shape[shapeID][houseID];
	const int flip = flags & 0x03;

	if (s_shape_bene[shapeID])
		s_frame_uses_bene = true;

	if ((flags & 0x300) == 0x100) {
		/* Highlight: Allegro 5 adds the shape on top of itself. */
		VideoDOS_Draw(bmp, x, y, flip, BLIT_LUT, s_brighten, 0);
	} else if ((flags & 0x300) == 0x200) {
		/* Blur tile (sandworm, sonic wave): shift what is underneath,
		 * as the original game did. */
		const int s_variable_60[8] = {1, 3, 2, 5, 4, 3, 2, 1};
		const int effect = (flags >> 4) & 0x7;
		int shift, unused;

		VideoDOS_ScaleToScreen(s_variable_60[effect], 0, &shift, &unused);
		VideoDOS_Draw(bmp, x, y, flip, BLIT_SHIFT, NULL, max(1, shift));
	} else if ((flags & 0x300) == 0x300) {
		/* Shadow. */
		VideoDOS_Draw(bmp, x, y, flip, BLIT_SHADE, VideoDOS_ShadeTable(flags & 0xF0), 0);
	} else {
		/* Normal. */
		VideoDOS_Draw(bmp, x, y, flip, BLIT_MASKED, NULL, 0);
	}
}

void
VideoA5_DrawShapeRotate(enum ShapeID shapeID, enum HouseType houseID, int x, int y, int orient256, int flags)
{
	BITMAP *bmp = s_shape[shapeID][houseID];
	assert(shapeID < SHAPEID_MAX);
	assert(houseID < HOUSE_NEUTRAL);
	assert(bmp != NULL);
	assert((flags & 0x300) != 0x100);
	assert((flags & 0x300) != 0x200);

	/* Rotate into a scratch bitmap at 1:1, then draw through the
	 * blitter so ScreenDiv scaling and shading still apply. */
	const int size = 2 * max(bmp->w, bmp->h);
	if (scratch == NULL || scratch->w < size || scratch->h < size) {
		if (scratch != NULL)
			destroy_bitmap(scratch);
		scratch = VideoDOS_CreateBitmap(max(size, 64), max(size, 64));
	}

	clear_bitmap(scratch);
	const fixed angle = itofix(orient256 & 0xFF);

	if (flags & 0x02) {
		pivot_sprite_v_flip(scratch, bmp, size / 2, size / 2, bmp->w / 2, bmp->h / 2, angle);
	} else {
		pivot_sprite(scratch, bmp, size / 2, size / 2, bmp->w / 2, bmp->h / 2, angle);
	}

	const enum BlitMode mode = ((flags & 0x300) == 0x300) ? BLIT_SHADE : BLIT_MASKED;
	const unsigned char *lut = (mode == BLIT_SHADE) ? VideoDOS_ShadeTable(flags & 0xF0) : NULL;

	VideoDOS_Blit(scratch, 0, 0, size, size, x - size / 2, y - size / 2, size, size, 0, mode, lut, 0);
}

void
VideoA5_DrawShapeScale(enum ShapeID shapeID, int x, int y, int w, int h, int flags)
{
	assert(shapeID < SHAPEID_MAX);

	BITMAP *bmp = s_shape[shapeID][HOUSE_HARKONNEN];
	assert(bmp != NULL);

	VideoDOS_Blit(bmp, 0, 0, bmp->w, bmp->h, x, y, w, h, flags & 0x03, BLIT_MASKED, NULL, 0);
}

void
VideoA5_DrawShapeGrey(enum ShapeID shapeID, int x, int y, int flags)
{
	const enum ShapeID greyID = SHAPE_CONCRETE_SLAB_GREY + (shapeID - SHAPE_CONCRETE_SLAB);
	assert(SHAPE_CONCRETE_SLAB <= shapeID && shapeID <= SHAPE_SANDWORM);
	assert(s_shape[greyID][HOUSE_HARKONNEN] != NULL);

	VideoDOS_Draw(s_shape[greyID][HOUSE_HARKONNEN], x, y, flags & 0x03, BLIT_MASKED, NULL, 0);
}

void
VideoA5_DrawShapeGreyScale(enum ShapeID shapeID, int x, int y, int w, int h, int flags)
{
	const enum ShapeID greyID = SHAPE_CONCRETE_SLAB_GREY + (shapeID - SHAPE_CONCRETE_SLAB);
	assert(SHAPE_CONCRETE_SLAB <= shapeID && shapeID <= SHAPE_SANDWORM);

	BITMAP *bmp = s_shape[greyID][HOUSE_HARKONNEN];
	assert(bmp != NULL);

	VideoDOS_Blit(bmp, 0, 0, bmp->w, bmp->h, x, y, w, h, flags & 0x03, BLIT_MASKED, NULL, 0);
}

void
VideoA5_DrawShapeTint(enum ShapeID shapeID, int x, int y, unsigned char c, int flags)
{
	assert(shapeID < SHAPEID_MAX);
	assert(s_shape[shapeID][HOUSE_HARKONNEN] != NULL);

	/* Tinted shapes are white masks (map arrows): draw in colour c. */
	VideoDOS_Draw(s_shape[shapeID][HOUSE_HARKONNEN], x, y, flags & 0x03, BLIT_SOLID, NULL, c);
}

FadeInAux *
Video_InitFadeInShape(enum ShapeID shapeID, enum HouseType houseID, int x, int y)
{
	assert(shapeID < SHAPEID_MAX);
	assert(houseID < HOUSE_NEUTRAL);
	assert(s_shape[shapeID][houseID] != NULL);

	BITMAP *src = s_shape[shapeID][houseID];

	return VideoDOS_InitFadeInSprite(src, true, x, y, src->w, src->h, true);
}

/*--------------------------------------------------------------*/

static int
VideoDOS_FontIndex(const Font *font, const uint8 *pal)
{
	if (font == g_fontNew6p) {
		/* 4 colour font. */
		if (memcmp(pal+2, font_palette[0]+2, 2) == 0) return 0;
		if (memcmp(pal+2, font_palette[1]+2, 2) == 0) return 1;
		if (memcmp(pal+2, font_palette[2]+2, 2) == 0) return 2;
	} else if (font == g_fontNew8p) {
		/* 4 colour font. */
		if (memcmp(pal+2, font_palette[0]+2, 2) == 0) return 3;
		if (memcmp(pal+2, font_palette[1]+2, 2) == 0) return 4;
		if (memcmp(pal+2, font_palette[2]+2, 2) == 0) return 5;
	} else {
		/* 7 colour font. */
		assert(font == g_fontIntro);
		if (memcmp(pal+2, font_palette[0]+2, 5) == 0) return 6;
		if (memcmp(pal+2, font_palette[3]+2, 5) == 0) return 7;
	}

	return 0;
}

static void
VideoDOS_ExportFont(Font *font, const uint8 *pal, int y, int *rety)
{
	const int WINDOW_W = 512;
	const int WINDOW_H = 1024;
	const int fnt = VideoDOS_FontIndex(font, pal);

	int x = 0;

	Font_Select(font);
	GUI_InitColors(pal, 0, 15);

	for (int c = 0; c < 256; c++) {
		if ((c < font->count) && (font->chars[c].data != NULL)) {
			/* Image width is Font_GetCharWidth(c) + 1. */
			const int w = Font_GetCharWidth(c) + 1;

			VideoDOS_GetNextXY(WINDOW_W, WINDOW_H, x, y, w, font->height, font->height, &x, &y);
			GUI_DrawChar_(c, x, y);

			s_font[fnt][c] = VideoDOS_SubBitmap(interface_texture, x, y, w, font->height);
			assert(s_font[fnt][c] != NULL);

			x += w + 1;
		}
	}

	*rety = y + font->height + 1;
}

static void
VideoDOS_InitFonts(unsigned char *buf)
{
	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	int y = 512;

	VideoDOS_ExportFont(g_fontNew6p, font_palette[0], y, &y);
	VideoDOS_ExportFont(g_fontNew6p, font_palette[1], y, &y);
	VideoDOS_ExportFont(g_fontNew6p, font_palette[2], y, &y);
	VideoDOS_ExportFont(g_fontNew8p, font_palette[0], y, &y);
	VideoDOS_ExportFont(g_fontNew8p, font_palette[1], y, &y);
	VideoDOS_ExportFont(g_fontNew8p, font_palette[2], y, &y);
	VideoDOS_ExportFont(g_fontIntro, font_palette[0], y, &y);
	VideoDOS_ExportFont(g_fontIntro, font_palette[3], y, &y);

	VideoDOS_CopyBitmap(WINDOW_W, buf, interface_texture, SKIP_COLOUR_0);
}

void
VideoA5_DrawChar(unsigned char c, const uint8 *pal, int x, int y)
{
	const int fnt = VideoDOS_FontIndex(g_fontCurrent, pal);

	/* Glyphs are white (0xFF) with dark outlines; Allegro 5 tints them
	 * by the foreground colour, which leaves the outline dark. */
	static unsigned char lut[256];
	static bool lut_init;

	if (!lut_init) {
		for (int i = 0; i < 256; i++)
			lut[i] = i;
		lut_init = true;
	}

	lut[0xFF] = pal[1];

	if (s_font[fnt][c] != NULL)
		VideoDOS_Draw(s_font[fnt][c], x, y, 0, BLIT_LUT, lut, 0);
}

void
VideoA5_DrawCharAlpha(unsigned char c, const uint8 *pal, int x, int y, unsigned char alpha)
{
	/* No translucent text in 8bpp: show it once mostly faded in. */
	if (alpha >= 0x80)
		VideoA5_DrawChar(c, pal, x, y);
}

/*--------------------------------------------------------------*/

static void
VideoDOS_InitWSA(void)
{
	void *wsa = WSA_LoadFile("STATIC.WSA", GFX_Screen_Get_ByIndex(SCREEN_2), GFX_Screen_GetSize_ByIndex(SCREEN_2), true);
	assert(wsa != NULL);

	const int num_frames = min(WSA_GetFrameCount(wsa), WSA_STATIC_MAX);
	const unsigned char *buf = GFX_Screen_Get_ByIndex(SCREEN_0);

	for (int frame = 0; frame < num_frames; frame++) {
		WSA_DisplayFrame(wsa, frame, 0, 0, SCREEN_0);

		s_wsa_static[frame] = VideoDOS_CreateBitmap(64, 64);
		VideoDOS_CopyBitmap(SCREEN_WIDTH, buf, s_wsa_static[frame], BLACK_COLOUR_0);
	}

	WSA_Unload(wsa);
}

bool
VideoA5_DrawWSA(void *wsa, int frame, int sx, int sy, int dx, int dy, int w, int h)
{
	if (wsa != NULL) {
		if (!WSA_DisplayFrame(wsa, frame, 0, 0, SCREEN_0))
			return false;
	}

	/* Draw straight from the 320 wide SCREEN_0 buffer. */
	const Src8 src = { GFX_Screen_Get_ByIndex(SCREEN_0), SCREEN_WIDTH };
	VideoDOS_BlitSrc(&src, sx, sy, w, h, dx, dy, w, h, 0, BLIT_OPAQUE, NULL, 0);

	return true;
}

void
VideoA5_DrawWSAStatic(int frame, int x, int y)
{
	assert(0 <= frame && frame < WSA_STATIC_MAX);

	VideoDOS_Draw(s_wsa_static[frame], x, y, 0, BLIT_OPAQUE, NULL, 0);
}

/*--------------------------------------------------------------*/

void
Video_DrawMinimap(int left, int top, int map_scale, enum MinimapDrawMode mode)
{
	const MapInfo *mapInfo = &g_mapInfos[map_scale];
	bool redraw = false;
	int sandworm_position[4 * 2];
	int num_sandworms = 0;

	if (mode == MINIMAP_RESTORE) {
		VideoDOS_Blit(s_minimap, 0, 0, mapInfo->sizeX, mapInfo->sizeY,
				left, top, (map_scale + 1.0f) * mapInfo->sizeX, (map_scale + 1.0f) * mapInfo->sizeY, 0, BLIT_OPAQUE, NULL, 0);
		return;
	}

	for (int y = 0; y < mapInfo->sizeY; y++) {
		uint16 packed = Tile_PackXY(mapInfo->minX, mapInfo->minY + y);
		int i = mapInfo->sizeX * y;

		for (int x = 0; x < mapInfo->sizeX; x++, i++, packed++) {
			const Tile *t = &g_map[packed];
			int colour = 12;

			if (mode == 1) {
				uint16 type = Map_GetLandscapeTypeOriginal(packed);
				colour = g_table_landscapeInfo[type].radarColour;
			} else if (g_playerHouse->flags.radarActivated
					&& Map_IsUnveiledToHouse(g_playerHouseID, packed)) {
				Unit *u;

				if (enhancement_fog_of_war && g_mapVisible[packed].timeout[g_playerHouseID] <= g_timerGame) {
				} else if (t->hasUnit && ((u = Unit_Get_ByPackedTile(packed)) != NULL)) {
					if (u->o.type == UNIT_SANDWORM) {
						/* Really shouldn't have more than 3, but anyway. */
						if (num_sandworms < 4) {
							sandworm_position[2*num_sandworms + 0] = x;
							sandworm_position[2*num_sandworms + 1] = y;
							num_sandworms++;
						}
					} else {
						colour = g_table_houseInfo[Unit_GetHouseID(u)].minimapColor;
					}
				}

				if (colour == 12) {
					uint16 type = Map_GetLandscapeTypeVisible(packed);

					if (g_table_landscapeInfo[type].radarColour == 0xFFFF) {
						colour = g_table_houseInfo[t->houseID].minimapColor;
					} else if (enhancement_fog_of_war && g_mapVisible[packed].timeout[g_playerHouseID] <= g_timerGame) {
						colour = -g_table_landscapeInfo[type].radarColour;
					} else {
						colour = g_table_landscapeInfo[type].radarColour;
					}
				}
			} else if (t->hasStructure && t->houseID == g_playerHouseID) {
				colour = g_table_houseInfo[t->houseID].minimapColor;
			}

			if (s_minimap_colour[i] != colour) {
				s_minimap_colour[i] = colour;
				redraw = true;
			}
		}
	}

	if (redraw) {
		/* Negative colour denotes darkened for fog of war. */
		const unsigned char *half = VideoDOS_ShadeTable(0x80);

		for (int y = 0; y < mapInfo->sizeY; y++) {
			unsigned char *row = s_minimap->line[y];

			for (int x = 0; x < mapInfo->sizeX; x++) {
				const int c = s_minimap_colour[mapInfo->sizeX * y + x];
				row[x] = (c >= 0) ? c : half[-c];
			}
		}
	}

	VideoDOS_Blit(s_minimap, 0, 0, mapInfo->sizeX, mapInfo->sizeY,
			left, top, (map_scale + 1.0f) * mapInfo->sizeX, (map_scale + 1.0f) * mapInfo->sizeY, 0, BLIT_OPAQUE, NULL, 0);

	/* Always redraw sandworms because they glow. */
	for (int i = 0; i < num_sandworms; i++) {
		const float x1 = left + (map_scale + 1.0f) * sandworm_position[2*i + 0];
		const float y1 = top  + (map_scale + 1.0f) * sandworm_position[2*i + 1];

		Prim_FillRect(x1, y1, x1 + map_scale + 1.0f, y1 + map_scale + 1.0f, 0xFF);
	}
}

/*--------------------------------------------------------------*/

int
VideoA5_GetDesktopWidth(void)
{
	return TRUE_DISPLAY_WIDTH;
}

int
VideoA5_GetDesktopHeight(void)
{
	return TRUE_DISPLAY_HEIGHT;
}

void
Video_InitMentatSprites(bool use_benepal)
{
	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	const int TEXTURE_W = 256;
	const int TEXTURE_H = 256;
	const enum WindowID old_widget = Widget_SetCurrentWidget(WINDOWID_RENDER_TEXTURE);
	const uint16 old_screen = GFX_Screen_SetActive(0);

	unsigned char *buf = GFX_Screen_GetActive();
	memset(buf, 0, WINDOW_W * TEXTURE_H);

	for (int i = 0; i < 15; i++) {
		const enum ShapeID shapeID = SHAPE_MENTAT_EYES + i;

		if (s_shape[shapeID][HOUSE_HARKONNEN] != NULL)
			destroy_bitmap(s_shape[shapeID][HOUSE_HARKONNEN]);
		s_shape[shapeID][HOUSE_HARKONNEN] = NULL;
	}

	if (mentat_texture != NULL) {
		VideoDOS_ForgetSubBitmaps(mentat_texture);
		destroy_bitmap(mentat_texture);
	}
	mentat_texture = VideoDOS_CreateBitmap(TEXTURE_W, TEXTURE_H);

	GUI_Palette_CreateRemap(HOUSE_HARKONNEN);

	int x = 1;
	int y = 1;
	int row_h = 0;

	for (int i = 0; i < 15; i++) {
		const enum ShapeID shapeID = SHAPE_MENTAT_EYES + i;

		s_shape[shapeID][HOUSE_HARKONNEN] = VideoDOS_ExportShape(mentat_texture, shapeID, x, y, row_h, &x, &y, &row_h, g_remap);
		s_shape_bene[shapeID] = use_benepal;
	}

	VideoDOS_CopyBitmap(WINDOW_W, buf, mentat_texture, TRANSPARENT_COLOUR_0);

	GFX_Screen_SetActive(old_screen);
	Widget_SetCurrentWidget(old_widget);
}

void
VideoA5_InitSprites(void)
{
	const int WINDOW_W = g_widgetProperties[WINDOWID_RENDER_TEXTURE].width;
	const int WINDOW_H = g_widgetProperties[WINDOWID_RENDER_TEXTURE].height;
	const Screen oldScreenID = GFX_Screen_SetActive(SCREEN_0);
	const enum WindowID old_widget = Widget_SetCurrentWidget(WINDOWID_RENDER_TEXTURE);

	unsigned char *buf = GFX_Screen_GetActive();

	PALETTE ibm;
	VideoDOS_LoadPalette("IBM.PAL", ibm);
	VideoDOS_LoadPalette("BENE.PAL", s_bene_palette);
	VideoDOS_BuildColourTables(ibm);

	/* As VideoA5_ReadPalette("IBM.PAL"): start with the game palette. */
	memcpy(s_game_palette, ibm, sizeof(PALETTE));

	memset(buf, 0, WINDOW_W * WINDOW_H);
	VideoDOS_InitIcons(buf);
	/* New icon graphics: the kept terrain layer is stale. */
	memset(s_terrain_geometry, 0, sizeof(s_terrain_geometry));
	VideoDOS_TerrainInvalidate();

	memset(buf, 0, WINDOW_W * WINDOW_H);
	VideoDOS_InitShapes(buf);

	memset(buf, 0, WINDOW_W * WINDOW_H);
	VideoDOS_InitCPS();

	memset(buf, 0, WINDOW_W * WINDOW_H);
	VideoDOS_InitWSA();

	memset(buf, 0, WINDOW_W * WINDOW_H);
	VideoDOS_InitFonts(buf);

	GFX_Screen_SetActive(oldScreenID);
	Widget_SetCurrentWidget(old_widget);

	VideoDOS_TrimAtlas(&interface_texture);
	VideoDOS_TrimAtlas(&shape_texture);
	VideoDOS_TrimAtlas(&region_texture);

	_go32_dpmi_meminfo info;
	if (_go32_dpmi_get_free_memory_information(&info) == 0) {
		RunLog_Printf("Memory: %lu KB of %lu KB physical free after loading graphics\n",
			(unsigned long)info.available_physical_pages * 4,
			(unsigned long)info.total_physical_pages * 4);
	}
}

void
VideoA5_DisplayFound(void)
{
	memset(s_terrain_geometry, 0, sizeof(s_terrain_geometry));
	VideoDOS_TerrainInvalidate();
	VideoDOS_UninitCPSStore();
	memset(s_minimap_colour, 0, sizeof(s_minimap_colour));
}

int
VideoA5_GetHeight(enum ShapeID shapeID)
{
	if (shapeID == SHAPE_INVALID || s_shape[shapeID][HOUSE_HARKONNEN] == NULL)
		return 0;

	return s_shape[shapeID][HOUSE_HARKONNEN]->h;
}

int
VideoA5_GetWidth(enum ShapeID shapeID)
{
	if (shapeID == SHAPE_INVALID || s_shape[shapeID][HOUSE_HARKONNEN] == NULL)
		return 0;

	return s_shape[shapeID][HOUSE_HARKONNEN]->w;
}

struct DisplayMode *
VideoA5_GetDisplayModes(void)
{
	return s_display_modes;
}

int
VideoA5_GetNumDisplayModes(void)
{
	return sizeof(s_display_modes) / sizeof(s_display_modes[0]);
}

int
VideoA5_GetCurrentDisplayMode(void)
{
	for (int i = 0; i < VideoA5_GetNumDisplayModes(); i++) {
		if (s_display_modes[i].width == TRUE_DISPLAY_WIDTH && s_display_modes[i].height == TRUE_DISPLAY_HEIGHT)
			return i;
	}

	return 0;
}
