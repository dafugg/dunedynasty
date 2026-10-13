#ifndef VIDEO_UICACHE_DOS_H
#define VIDEO_UICACHE_DOS_H

#include <stdbool.h>

/* MS-DOS: the menubar and sidebar are redrawn at 15 Hz, or at once
 * after input, and copied from the last drawing in between.
 *
 *   if (VideoDOS_UIBegin()) { draw menubar and sidebar; VideoDOS_UIStore(); }
 */
extern bool VideoDOS_UIBegin(void);
extern void VideoDOS_UIStore(void);

/* While the game is paused behind a hint or the options menu, the
 * shaded game screen is kept and copied back instead of redrawn:
 *
 *   if (!VideoDOS_PausedBegin()) { draw the game screen; }
 *   draw the overlay (its Video_ShadeScreen is skipped if restored);
 *
 * Any frame drawn without VideoDOS_PausedBegin() forgets the copy. */
extern bool VideoDOS_PausedBegin(void);

/* Changes on every key press and mouse button change (input_dos.c). */
extern unsigned int g_dos_input_serial;

#endif
