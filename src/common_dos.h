#ifndef COMMON_DOS_H
#define COMMON_DOS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Command line switches, e.g. DUNEDYN -nosound -voices 4. */
extern bool DOS_HasArg(const char *name);
extern const char *DOS_ArgValue(const char *name);

/* DUNEDYN.LOG reports (sysinfo_dos.c). */
extern void DOS_LogSystemReport(void);
extern void DOS_LogSettings(void);

#ifdef __cplusplus
}
#endif

#endif
