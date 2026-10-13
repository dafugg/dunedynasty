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

/* Changes on every key press and mouse button change (input_dos.c). */
extern unsigned int g_dos_input_serial;

#endif
