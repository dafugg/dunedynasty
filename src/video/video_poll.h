#ifndef VIDEO_VIDEO_POLL_H
#define VIDEO_VIDEO_POLL_H

/* Long work in the main program, such as generating a skirmish map:
 * VIDEO_POLL_CURSOR() lets a platform whose mouse cursor is drawn by
 * the main program keep it moving.  No-op except on MS-DOS. */
#ifdef __DJGPP__
extern void VideoDOS_PollCursor(void);
#define VIDEO_POLL_CURSOR()     VideoDOS_PollCursor()
#else
#define VIDEO_POLL_CURSOR()
#endif

#endif
