/* prim_dos.c
 *
 * Drawing primitives for the MS-DOS 8bpp software renderer.
 * Coordinates are in the current ScreenDiv, as with the Allegro 5
 * transforms; colours are palette indices.
 *
 * The _RGBA variants map to the nearest palette colour.  Phase 2
 * replaces translucent drawing with shade lookup tables.
 */

#include <allegro.h>
#include <assert.h>

#include "prim.h"
#include "video_dos.h"

/*--------------------------------------------------------------*/

/* Fill the half-open logical rectangle [x1, x2) x [y1, y2). */
static void
PrimDOS_Fill(float x1, float y1, float x2, float y2, int c)
{
	int sx1, sy1, sx2, sy2;

	VideoDOS_ToScreen(x1, y1, &sx1, &sy1);
	VideoDOS_ToScreen(x2, y2, &sx2, &sy2);

	if (sx2 > sx1 && sy2 > sy1)
		rectfill(g_dos_backbuffer, sx1, sy1, sx2 - 1, sy2 - 1, c);
}

/* Outline the half-open logical rectangle [x1, x2) x [y1, y2). */
static void
PrimDOS_Outline(float x1, float y1, float x2, float y2, int c)
{
	int sx1, sy1, sx2, sy2;

	VideoDOS_ToScreen(x1, y1, &sx1, &sy1);
	VideoDOS_ToScreen(x2, y2, &sx2, &sy2);

	if (sx2 > sx1 && sy2 > sy1)
		rect(g_dos_backbuffer, sx1, sy1, sx2 - 1, sy2 - 1, c);
}

static int
PrimDOS_RGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char alpha)
{
	if (alpha == 0)
		return -1;

	return VideoDOS_NearestColour(r, g, b);
}

/*--------------------------------------------------------------*/

void
Prim_Line(float x1, float y1, float x2, float y2, uint8 c, float thickness)
{
	int sx1, sy1, sx2, sy2;

	VARIABLE_NOT_USED(thickness);

	VideoDOS_ToScreen(x1, y1, &sx1, &sy1);
	VideoDOS_ToScreen(x2, y2, &sx2, &sy2);
	line(g_dos_backbuffer, sx1, sy1, sx2, sy2, c);
}

void
Prim_Hline(int x1, int y, int x2, uint8 c)
{
	assert(x1 <= x2);
	PrimDOS_Fill(x1, y, x2 + 1, y + 1, c);
}

void
Prim_Vline(int x, int y1, int y2, uint8 c)
{
	assert(y1 <= y2);
	PrimDOS_Fill(x, y1, x + 1, y2 + 1, c);
}

/*--------------------------------------------------------------*/

void
Prim_Circle(float cx, float cy, float rd, uint8 c, float thickness)
{
	int sx, sy, sr, unused;

	VARIABLE_NOT_USED(thickness);

	VideoDOS_ToScreen(cx, cy, &sx, &sy);
	VideoDOS_ScaleToScreen(rd, rd, &sr, &unused);
	circle(g_dos_backbuffer, sx, sy, sr, c);
}

void
Prim_Circle_i(int cx, int cy, int rd, uint8 c)
{
	Prim_Circle(cx + 0.5f, cy + 0.5f, rd + 0.5f, c, 1.0f);
}

void
Prim_Circle_RGBA(float cx, float cy, float rd, unsigned char r, unsigned char g, unsigned char b, unsigned char alpha, float thickness)
{
	const int c = PrimDOS_RGBA(r, g, b, alpha);
	if (c >= 0)
		Prim_Circle(cx, cy, rd, c, thickness);
}

/*--------------------------------------------------------------*/

void
Prim_FillCircle(float cx, float cy, float rd, uint8 c)
{
	int sx, sy, sr, unused;

	VideoDOS_ToScreen(cx, cy, &sx, &sy);
	VideoDOS_ScaleToScreen(rd, rd, &sr, &unused);
	circlefill(g_dos_backbuffer, sx, sy, sr, c);
}

void
Prim_FillCircle_i(int cx, int cy, int rd, uint8 c)
{
	Prim_FillCircle(cx + 0.5f, cy + 0.5f, rd + 0.5f, c);
}

void
Prim_FillCircle_RGBA(float cx, float cy, float rd, unsigned char r, unsigned char g, unsigned char b, unsigned char alpha)
{
	const int c = PrimDOS_RGBA(r, g, b, alpha);
	if (c >= 0)
		Prim_FillCircle(cx, cy, rd, c);
}

/*--------------------------------------------------------------*/

void
Prim_Rect(float x1, float y1, float x2, float y2, uint8 c, float thickness)
{
	VARIABLE_NOT_USED(thickness);
	PrimDOS_Outline(x1, y1, x2, y2, c);
}

void
Prim_Rect_i(int x1, int y1, int x2, int y2, uint8 c)
{
	assert(x1 <= x2);
	assert(y1 <= y2);

	PrimDOS_Outline(x1, y1, x2 + 1, y2 + 1, c);
}

void
Prim_Rect_RGBA(float x1, float y1, float x2, float y2, unsigned char r, unsigned char g, unsigned char b, unsigned char alpha, float thickness)
{
	const int c = PrimDOS_RGBA(r, g, b, alpha);
	if (c >= 0)
		Prim_Rect(x1, y1, x2, y2, c, thickness);
}

/*--------------------------------------------------------------*/

void
Prim_FillRect(float x1, float y1, float x2, float y2, uint8 c)
{
	PrimDOS_Fill(x1, y1, x2, y2, c);
}

void
Prim_FillRect_i(int x1, int y1, int x2, int y2, uint8 c)
{
	assert(x1 <= x2);
	assert(y1 <= y2);

	PrimDOS_Fill(x1, y1, x2 + 1, y2 + 1, c);
}

void
Prim_FillRect_RGBA(float x1, float y1, float x2, float y2,
		unsigned char r, unsigned char g, unsigned char b, unsigned char alpha)
{
	assert(x1 <= x2);
	assert(y1 <= y2);

	const int c = PrimDOS_RGBA(r, g, b, alpha);
	if (c >= 0)
		PrimDOS_Fill(x1, y1, x2, y2, c);
}

/*--------------------------------------------------------------*/

extern uint8 s_colourBorderSchema[5][4];

/**
 * Dune II bevelled border.  Drawn in the same order as the Allegro 5
 * triangle list: outline, background, top/left, bottom/right, corners.
 */
void
Prim_DrawBorder(float x, float y, float w, float h, int thickness, bool outline, bool fill, int colour_scheme)
{
	const uint8 *scheme = s_colourBorderSchema[colour_scheme];
	const float sz = thickness;

	if (!fill) {
		if (outline)
			Prim_Rect(x - 1.0f, y - 1.0f, x + w + 1.0f, y + h + 1.0f, 12, 1.0f);
	} else {
		if (outline)
			PrimDOS_Fill(x - 1.0f, y - 1.0f, x + w + 1.0f, y + h + 1.0f, 12);

		PrimDOS_Fill(x, y, x + w, y + h, scheme[0]);
	}

	/* Top, left borders. */
	PrimDOS_Fill(x, y, x + w, y + sz, scheme[2]);
	PrimDOS_Fill(x, y, x + sz, y + h, scheme[2]);

	/* Bottom, right borders. */
	PrimDOS_Fill(x + w - sz, y + sz, x + w, y + h, scheme[1]);
	PrimDOS_Fill(x + sz, y + h - sz, x + w, y + h, scheme[1]);

	/* Corners. */
	PrimDOS_Fill(x + w - 1.0f, y, x + w, y + 1.0f, scheme[3]);
	PrimDOS_Fill(x, y + h - 1.0f, x + 1.0f, y + h, scheme[3]);

	if (thickness > 1) {
		PrimDOS_Fill(x + w - 2.0f, y + 1.0f, x + w - 1.0f, y + 2.0f, scheme[3]);
		PrimDOS_Fill(x + 1.0f, y + h - 2.0f, x + 2.0f, y + h - 1.0f, scheme[3]);
	}
}
