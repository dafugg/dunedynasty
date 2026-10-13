/* input_dos.c
 *
 * MS-DOS keyboard and mouse on Allegro 4.  The keyboard ISR callback
 * only queues raw key codes; translation to Dune Dynasty scancodes and
 * all event handling happen in the main loop.
 */

#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include "errorlog.h"

#include "input_a5.h"

#include "../config.h"
#include "../input/input.h"
#include "../input/mouse.h"
#include "../timer/timer_dos.h"
#include "../scenario.h"
#include "../sprites.h"
#include "../newui/viewport.h"
#include "../opendune.h"
#include "../tools/coord.h"
#include "../map.h"
#include "../unit.h"
#include "../structure.h"
#include "../gui/gui.h"
#include "../pool/pool.h"
#include "../pool/pool_unit.h"
#include "../pool/pool_structure.h"
#include "../video/uicache_dos.h"
#include "../video/video.h"
#include "scancode.h"

enum {
	KEY_QUEUE_SIZE = 256    /* Power of two. */
};

/* Written by the keyboard ISR. */
static volatile unsigned char s_key_queue[KEY_QUEUE_SIZE];
static volatile unsigned int s_key_head;

/* Main loop only. */
static unsigned int s_key_tail;
static int s_mouse_x;
static int s_mouse_y;
static int s_mouse_b;
static int s_mouse_z;
static bool s_apply_mouse_transform = true;     /* As last requested by the game. */
static bool s_mouse_log;                        /* DD_MOUSELOG: trace mouse movement. */

static void
InputDOS_KeyboardCallback(int scancode)
{
	s_key_queue[s_key_head & (KEY_QUEUE_SIZE - 1)] = (unsigned char)scancode;
	s_key_head++;
}
END_OF_STATIC_FUNCTION(InputDOS_KeyboardCallback)

bool
InputA5_Init(void)
{
	LOCK_VARIABLE(s_key_queue);
	LOCK_VARIABLE(s_key_head);
	LOCK_FUNCTION(InputDOS_KeyboardCallback);

	if (install_keyboard() != 0) {
		Error("install_keyboard() failed.\n");
		return false;
	}

	/* Game code reads key state from Input_Test, not from BIOS. */
	set_keyboard_rate(0, 0);
	keyboard_lowlevel_callback = InputDOS_KeyboardCallback;

	if (install_mouse() < 0) {
		Error("No mouse driver found.  Load CTMOUSE or MOUSE.COM.\n");
		return false;
	}

	s_mouse_x = mouse_x;
	s_mouse_y = mouse_y;
	s_mouse_b = mouse_b;
	s_mouse_z = mouse_z;
	s_mouse_log = (getenv("DD_MOUSELOG") != NULL);
	return true;
}

void
InputA5_Uninit(void)
{
	keyboard_lowlevel_callback = NULL;
	remove_mouse();
	remove_keyboard();
}

static int
InputDOS_KeycodeToScancode(int kc)
{
	static const int map[26 + 10 + 10 + 12] = {
		SCANCODE_A, SCANCODE_B, SCANCODE_C, SCANCODE_D, SCANCODE_E,
		SCANCODE_F, SCANCODE_G, SCANCODE_H, SCANCODE_I, SCANCODE_J,
		SCANCODE_K, SCANCODE_L, SCANCODE_M, SCANCODE_N, SCANCODE_O,
		SCANCODE_P, SCANCODE_Q, SCANCODE_R, SCANCODE_S, SCANCODE_T,
		SCANCODE_U, SCANCODE_V, SCANCODE_W, SCANCODE_X, SCANCODE_Y,
		SCANCODE_Z,

		SCANCODE_0, SCANCODE_1, SCANCODE_2, SCANCODE_3, SCANCODE_4,
		SCANCODE_5, SCANCODE_6, SCANCODE_7, SCANCODE_8, SCANCODE_9,

		SCANCODE_KEYPAD_0, SCANCODE_KEYPAD_1, SCANCODE_KEYPAD_2, SCANCODE_KEYPAD_3, SCANCODE_KEYPAD_4,
		SCANCODE_KEYPAD_5, SCANCODE_KEYPAD_6, SCANCODE_KEYPAD_7, SCANCODE_KEYPAD_8, SCANCODE_KEYPAD_9,

		SCANCODE_F1, SCANCODE_F2, SCANCODE_F3, SCANCODE_F4, SCANCODE_F5,
		SCANCODE_F6, SCANCODE_F7, SCANCODE_F8, SCANCODE_F9, SCANCODE_F10,
		SCANCODE_F11, SCANCODE_F12
	};

	/* Enter is mapped separately so that it is sent on press only,
	 * matching the KEY_CHAR handling of the Allegro 5 backend. */
	static const int map_misc[] = {
		SCANCODE_ESCAPE, SCANCODE_TILDE, SCANCODE_MINUS, SCANCODE_EQUALS, SCANCODE_BACKSPACE,
		SCANCODE_TAB, SCANCODE_OPENBRACE, SCANCODE_CLOSEBRACE, -1, SCANCODE_SEMICOLON,
		SCANCODE_QUOTE, SCANCODE_BACKSLASH, SCANCODE_BACKSLASH, SCANCODE_COMMA, SCANCODE_FULLSTOP,
		SCANCODE_SLASH, SCANCODE_SPACE,

		SCANCODE_INSERT, SCANCODE_DELETE, SCANCODE_HOME, SCANCODE_END, SCANCODE_PGUP,
		SCANCODE_PGDN, SCANCODE_LEFT, SCANCODE_RIGHT, SCANCODE_UP, SCANCODE_DOWN,

		SCANCODE_KEYPAD_SLASH, SCANCODE_KEYPAD_ASTERISK, SCANCODE_KEYPAD_MINUS, SCANCODE_KEYPAD_PLUS, SCANCODE_KEYPAD_DELETE,
		SCANCODE_KEYPAD_ENTER,

		SCANCODE_PRINTSCREEN
	};

	static const int map_mods[] = {
		SCANCODE_LSHIFT, SCANCODE_LSHIFT,
		SCANCODE_LCTRL, SCANCODE_RCTRL,
		SCANCODE_LALT, SCANCODE_RALT,
		SCANCODE_LWIN, SCANCODE_RWIN, SCANCODE_MENU,
		SCANCODE_SCROLLLOCK, SCANCODE_NUMLOCK, SCANCODE_CAPSLOCK
	};

	if (KEY_A <= kc && kc <= KEY_F12)
		return map[kc - KEY_A];

	if (KEY_ESC <= kc && kc <= KEY_PRTSCR)
		return map_misc[kc - KEY_ESC];

	if (KEY_LSHIFT <= kc && kc <= KEY_CAPSLOCK)
		return map_mods[kc - KEY_LSHIFT];

	return -1;
}

static bool
InputDOS_ProcessKey(unsigned char raw)
{
	const int kc = raw & 0x7F;
	const bool release = (raw & 0x80) != 0;

	if (kc == KEY_ENTER) {
		if (!release)
			Input_EventHandler(SCANCODE_ENTER);
		return !release;
	}

	if (!release) {
		if (kc == KEY_F10) {
			VideoA5_ToggleFPS();
			return true;
		} else if (kc == KEY_F12) {
			VideoA5_CaptureScreenshot();
			return true;
		}
	}

	int sc = InputDOS_KeycodeToScancode(kc);
	if (sc < 0)
		return false;

	if (release)
		sc |= SCANCODE_RELEASE;

	Input_EventHandler(sc);
	return false;
}

unsigned int g_dos_input_serial;

static bool
InputDOS_ProcessMouse(bool apply_mouse_transform)
{
	bool redraw = false;
	int mickey_x, mickey_y;

	get_mouse_mickeys(&mickey_x, &mickey_y);

	const int pos = mouse_pos;
	const int x = pos >> 16;
	const int y = pos & 0xFFFF;
	const int b = mouse_b;
	const int z = mouse_z;
	const int dz = z - s_mouse_z;
	s_mouse_z = z;

	const bool moved = (x != s_mouse_x) || (y != s_mouse_y);
	s_mouse_x = x;
	s_mouse_y = y;

	if (moved && s_mouse_log)
		printf("DD_MOUSELOG: mouse %d,%d mickeys %d,%d range y %d\n", x, y, mickey_x, mickey_y, SCREEN_H);

	if (mickey_x != 0 || mickey_y != 0 || dz != 0 || moved) {
		g_mouseDX += mickey_x;
		g_mouseDY += mickey_y;

		if (g_mousePanning) {
			Mouse_EventHandler(false, g_mouseX, g_mouseY, 0, 0);
		} else {
			Mouse_EventHandler(apply_mouse_transform, x, y, dz, 0);
		}

		redraw = true;
	}

	/* Allegro: bit 0 = left, bit 1 = right. */
	static const struct {
		int mask;
		enum Scancode code;
	} buttons[] = {
		{ 1, SCANCODE_MOUSE_LMB },
		{ 2, SCANCODE_MOUSE_RMB },
	};

	for (unsigned int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		const int mask = buttons[i].mask;

		if ((b & mask) == (s_mouse_b & mask))
			continue;

		enum Scancode state = buttons[i].code;
		if ((b & mask) == 0)
			state |= SCANCODE_RELEASE;

		g_dos_input_serial++;

		if (g_mousePanning) {
			Mouse_EventHandler(false, g_mouseX, g_mouseY, 0, state);
		} else {
			Mouse_EventHandler(apply_mouse_transform, x, y, 0, state);
		}

		redraw = true;
	}

	s_mouse_b = b;
	return redraw;
}

/**
 * Process all pending keyboard and mouse input.
 * @return true if a redraw is required.
 */
bool
InputA5_Tick(bool apply_mouse_transform)
{
	bool redraw = false;

	s_apply_mouse_transform = apply_mouse_transform;

	while (s_key_tail != s_key_head) {
		const unsigned char raw = s_key_queue[s_key_tail & (KEY_QUEUE_SIZE - 1)];
		s_key_tail++;

		g_dos_input_serial++;
		if (InputDOS_ProcessKey(raw))
			redraw = true;
	}

	if (InputDOS_ProcessMouse(apply_mouse_transform))
		redraw = true;

	return redraw;
}

/* The mouse position in screen pixels, as the hardware reports it. */
void
InputDOS_GetCursorPosition(int *x, int *y)
{
	*x = s_mouse_x;
	*y = s_mouse_y;
}

void
InputA5_FlushEvents(void)
{
	s_key_tail = s_key_head;
	TimerDOS_FlushInputTicks();

	int mickey_x, mickey_y;
	get_mouse_mickeys(&mickey_x, &mickey_y);
	s_mouse_x = mouse_x;
	s_mouse_y = mouse_y;
	s_mouse_b = mouse_b;
	s_mouse_z = mouse_z;
}

/**
 * Block until a GUI timer tick or input arrives, and process it.
 * Timer ticks are checked first so that a moving mouse cannot starve
 * the menu animations.
 * @return true if a redraw is required.
 */
bool
InputA5_WaitForEvent(bool apply_mouse_transform, bool *is_timer_event)
{
	s_apply_mouse_transform = apply_mouse_transform;

	for (;;) {
		if (TimerDOS_TakeInputTick()) {
			*is_timer_event = true;
			return false;
		}

		const int pos = mouse_pos;
		const bool mouse_changed = (mouse_b != s_mouse_b) || (mouse_z != s_mouse_z)
			|| ((pos >> 16) != s_mouse_x) || ((pos & 0xFFFF) != s_mouse_y);

		if (s_key_tail != s_key_head || mouse_changed) {
			*is_timer_event = false;
			return InputA5_Tick(apply_mouse_transform);
		}

		TimerDOS_Idle();
	}
}

/*--------------------------------------------------------------*/

/**
 * Scripted input for automated tests, from the environment:
 *   DD_AUTOINPUT=<tick>:<action>[,<tick>:<action>...]
 *   DD_AUTOINPUT=@<file>   the same, read from a file (entries may be
 *                          separated by line breaks).
 * where tick is in GUI timer ticks (1/60 s) and action is one of
 *   C<x>;<y>   left click at (x, y) in 320x200 menu coordinates, so
 *              the same script works at any screen resolution
 *   R<x>;<y>   right click
 *   M<x>;<y>   move the mouse
 *   K<hex>[;<n>]  press a Dune Dynasty scancode, release it after n
 *              ticks (default 4)
 *   H<hex>;<n> hold a scancode (e.g. Shift) for n ticks while the
 *              following actions run
 *   D<x1>;<y1>;<x2>;<y2>  drag with the left button from (x1, y1) to
 *              (x2, y2)
 *   S          centre the view on the player's construction yard (or
 *              MCV) and left click it
 */
void
InputDOS_PollAutomation(int64_t ticks)
{
	static const char *s_next;
	static bool s_init;
	static enum Scancode s_release;
	static int s_release_x, s_release_y;
	static int64_t s_release_tick;
	static enum Scancode s_hold;            /* H: modifier held alongside other actions. */
	static int64_t s_hold_tick;
	static int s_drag_x, s_drag_y;          /* D: where the held button moves to. */
	static int64_t s_drag_tick;

	if (!s_init) {
		s_next = getenv("DD_AUTOINPUT");
		s_init = true;

		/* @FILE reads the script from a file: DOS limits command lines. */
		if (s_next != NULL && s_next[0] == '@') {
			static char script[4096];
			FILE *fp = fopen(s_next + 1, "r");

			s_next = NULL;
			if (fp != NULL) {
				const size_t n = fread(script, 1, sizeof(script) - 1, fp);
				fclose(fp);

				/* Allow line breaks between entries; drop # comments. */
				bool comment = false;
				for (size_t i = 0; i < n; i++) {
					if (script[i] == '\r' || script[i] == '\n') {
						script[i] = ',';
						comment = false;
					} else if (script[i] == '#' || comment) {
						script[i] = ',';
						comment = true;
					}
				}
				script[n] = '\0';
				s_next = script;
			}
		}
	}

	/* Skip empty entries (from line breaks). */
	while (s_next != NULL && *s_next == ',')
		s_next++;

	/* Release what was pressed, after holding it for a few ticks so that
	 * the game loop sees the button down (drag selection checks state). */
	if (s_hold != 0 && ticks >= s_hold_tick) {
		Input_EventHandler(s_hold | SCANCODE_RELEASE);
		s_hold = 0;
	}

	if (s_drag_tick != 0 && ticks >= s_drag_tick) {
		position_mouse(s_drag_x, s_drag_y);
		s_mouse_x = mouse_x;
		s_mouse_y = mouse_y;
		Mouse_EventHandler(s_apply_mouse_transform, s_drag_x, s_drag_y, 0, 0);
		s_drag_tick = 0;
	}

	if (s_release != 0) {
		if (ticks < s_release_tick)
			return;

		if (s_release == SCANCODE_MOUSE_LMB || s_release == SCANCODE_MOUSE_RMB) {
			Mouse_EventHandler(s_apply_mouse_transform, s_release_x, s_release_y, 0, s_release | SCANCODE_RELEASE);
		} else {
			Input_EventHandler(s_release | SCANCODE_RELEASE);
		}
		s_release = 0;
		return;
	}

	if (s_next == NULL || *s_next == '\0')
		return;

	char *end;
	const long tick = strtol(s_next, &end, 10);
	if (ticks < tick)
		return;

	if (*end != ':') {
		s_next = NULL;
		return;
	}

	const char action = end[1];
	const char *arg = end + 2;

	if (action == 'H') {
		/* H<hex scancode>;<ticks>: hold a key (e.g. Shift) while the
		 * following actions run. */
		s_hold = strtol(arg, &end, 16);
		s_hold_tick = ticks + ((*end == ';') ? atoi(end + 1) : 4);
		Input_EventHandler(s_hold);
	} else if (action == 'K') {
		/* K<hex scancode>[;<ticks held>] */
		const enum Scancode sc = strtol(arg, &end, 16);
		const int hold = (*end == ';') ? atoi(end + 1) : 4;
		Input_EventHandler(sc);
		s_release = sc;
		s_release_tick = ticks + hold;
	} else {
		int x, y;

		if (action == 'S') {
			/* S: centre the view on the player's construction yard (or
			 * MCV) and click it, wherever a random map put it.  The view
			 * may stop at the map edge, so click where the object is. */
			const ScreenDiv *vp = &g_screenDiv[SCREENDIV_VIEWPORT];
			PoolFindStruct find;
			int px = 0, py = 0;

			const Structure *st = Structure_FindFirst(&find, g_playerHouseID, STRUCTURE_CONSTRUCTION_YARD);
			if (st != NULL) {
				px = (st->o.position.x >> 4) + TILE_SIZE;
				py = (st->o.position.y >> 4) + TILE_SIZE;
			} else {
				const Unit *u = Unit_FindFirst(&find, g_playerHouseID, UNIT_MCV);
				if (u != NULL) {
					px = u->o.position.x >> 4;
					py = u->o.position.y >> 4;
				}
			}

			Scenario_CentreViewport(g_playerHouseID);
			const int vx = TILE_SIZE * Tile_GetPackedX(g_viewportPosition) + g_viewport_scrollOffsetX;
			const int vy = TILE_SIZE * Tile_GetPackedY(g_viewportPosition) + g_viewport_scrollOffsetY;
			x = vp->x + (int)((px - vx) * vp->scalex);
			y = vp->y + (int)((py - vy) * vp->scaley);
		} else {
			/* Script coordinates are SCREENDIV_MENU (320x200) coordinates,
			 * converted to screen pixels and then fed in exactly as the
			 * hardware mouse would be. */
			const ScreenDiv *menu = &g_screenDiv[SCREENDIV_MENU];
			const int mx = strtol(arg, &end, 10);
			const int my = (*end == ';') ? strtol(end + 1, NULL, 10) : 0;
			x = menu->x + mx * menu->scalex;
			y = menu->y + my * menu->scaley;
		}

		position_mouse(x, y);
		s_mouse_x = mouse_x;
		s_mouse_y = mouse_y;

		if (action == 'D') {
			/* D<x1>;<y1>;<x2>;<y2>: drag with the left button. */
			const ScreenDiv *menu = &g_screenDiv[SCREENDIV_MENU];
			const char *p = strchr(arg, ';');
			p = (p != NULL) ? strchr(p + 1, ';') : NULL;

			if (p != NULL) {
				const int mx2 = strtol(p + 1, &end, 10);
				const int my2 = (*end == ';') ? strtol(end + 1, NULL, 10) : 0;
				s_drag_x = menu->x + mx2 * menu->scalex;
				s_drag_y = menu->y + my2 * menu->scaley;
				s_drag_tick = ticks + 3;
			}

			Mouse_EventHandler(s_apply_mouse_transform, x, y, 0, SCANCODE_MOUSE_LMB);
			s_release = SCANCODE_MOUSE_LMB;
			s_release_x = s_drag_x;
			s_release_y = s_drag_y;
			s_release_tick = ticks + 8;
		} else if (action == 'C' || action == 'R' || action == 'S') {
			const enum Scancode button = (action == 'R') ? SCANCODE_MOUSE_RMB : SCANCODE_MOUSE_LMB;

			Mouse_EventHandler(s_apply_mouse_transform, x, y, 0, button);
			s_release = button;
			s_release_x = x;
			s_release_y = y;

			/* Hold left for drag selection to notice; release right
			 * quickly, as holding it pans the viewport. */
			s_release_tick = ticks + ((action == 'R') ? 1 : 4);
		} else {
			Mouse_EventHandler(s_apply_mouse_transform, x, y, 0, 0);
		}
	}

	printf("DD_AUTOINPUT: tick %ld: %.*s\n", (long)ticks, (int)strcspn(s_next, ","), s_next);

	s_next = strchr(s_next, ',');
	if (s_next != NULL)
		s_next++;
}
