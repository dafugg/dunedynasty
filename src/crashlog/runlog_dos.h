#ifndef CRASHLOG_RUNLOG_DOS_H
#define CRASHLOG_RUNLOG_DOS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DUNEDYN.LOG next to the executable: start-up report, warnings,
 * errors and performance summaries.  The previous run's log is moved
 * to the end of DUNEDYN.OLD.  Each line is written through to disk, so
 * the log survives a crash or a reset. */
extern void RunLog_Open(const char *argv0);
extern void RunLog_Close(void);

/* Lines are time stamped and, when stdout is redirected (the test
 * scripts), echoed there too. */
extern void RunLog_Printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* While a graphics mode is set, nothing is echoed to the console (it
 * would draw on the screen); a reminder is printed afterwards instead. */
extern void RunLog_SetGraphicsMode(bool graphics);

#ifdef __cplusplus
}
#endif

#endif
