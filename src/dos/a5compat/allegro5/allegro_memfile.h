/** @file src/dos/a5compat/allegro5/allegro_memfile.h
 * MS-DOS only: Allegro 5 memory files, see allegro.h here. */

#ifndef DOS_A5COMPAT_ALLEGRO5_ALLEGRO_MEMFILE_H
#define DOS_A5COMPAT_ALLEGRO5_ALLEGRO_MEMFILE_H

#include "allegro.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ALLEGRO_FILE *al_open_memfile(void *mem, int64_t size, const char *mode);

#ifdef __cplusplus
}
#endif

#endif
