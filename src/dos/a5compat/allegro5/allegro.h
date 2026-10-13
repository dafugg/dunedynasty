/** @file src/dos/a5compat/allegro5/allegro.h
 *
 * MS-DOS only: the small part of Allegro 5's API that Dune Dynasty's
 * shared code calls directly (config files, paths, directory listing,
 * the menu's event wait and screen clear), implemented on DJGPP by
 * src/dos/a5compat.c.  The DOS build puts this directory on the include
 * path instead of Allegro 5, which has no DOS port; the platform layer
 * itself uses Allegro 4.
 *
 * Only what the shared code uses is declared; semantics follow
 * Allegro 5 for those calls.
 */

#ifndef DOS_A5COMPAT_ALLEGRO5_ALLEGRO_H
#define DOS_A5COMPAT_ALLEGRO5_ALLEGRO_H

/* Allegro 5's header brings these in, and the shared code relies on it. */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration files. */
typedef struct ALLEGRO_CONFIG ALLEGRO_CONFIG;

extern ALLEGRO_CONFIG *al_create_config(void);
extern ALLEGRO_CONFIG *al_load_config_file(const char *filename);
extern bool al_save_config_file(const char *filename, const ALLEGRO_CONFIG *config);
extern void al_destroy_config(ALLEGRO_CONFIG *config);
extern const char *al_get_config_value(const ALLEGRO_CONFIG *config, const char *section, const char *key);
extern void al_set_config_value(ALLEGRO_CONFIG *config, const char *section, const char *key, const char *value);
extern void al_add_config_comment(ALLEGRO_CONFIG *config, const char *section, const char *comment);

/* Paths.  DJGPP accepts '/' as well as '\'. */
#define ALLEGRO_NATIVE_PATH_SEP '/'

typedef struct ALLEGRO_PATH ALLEGRO_PATH;

enum {
	ALLEGRO_RESOURCES_PATH = 0,
	ALLEGRO_TEMP_PATH,
	ALLEGRO_USER_HOME_PATH,
	ALLEGRO_USER_DOCUMENTS_PATH,
	ALLEGRO_USER_DATA_PATH,
	ALLEGRO_USER_SETTINGS_PATH,
	ALLEGRO_EXENAME_PATH
};

extern ALLEGRO_PATH *al_create_path(const char *str);
extern ALLEGRO_PATH *al_create_path_for_directory(const char *str);
extern ALLEGRO_PATH *al_get_standard_path(int id);
extern void al_destroy_path(ALLEGRO_PATH *path);
extern const char *al_path_cstr(const ALLEGRO_PATH *path, char delim);
extern const char *al_get_path_tail(const ALLEGRO_PATH *path);
extern const char *al_get_path_filename(const ALLEGRO_PATH *path);
extern const char *al_get_path_basename(const ALLEGRO_PATH *path);
extern const char *al_get_path_extension(const ALLEGRO_PATH *path);
extern bool al_set_path_filename(ALLEGRO_PATH *path, const char *filename);
extern void al_replace_path_component(ALLEGRO_PATH *path, int i, const char *s);

/* Directories. */
typedef struct ALLEGRO_FS_ENTRY ALLEGRO_FS_ENTRY;

enum {
	ALLEGRO_FILEMODE_READ    = 1,
	ALLEGRO_FILEMODE_WRITE   = 1 << 1,
	ALLEGRO_FILEMODE_EXECUTE = 1 << 2,
	ALLEGRO_FILEMODE_HIDDEN  = 1 << 3,
	ALLEGRO_FILEMODE_ISFILE  = 1 << 4,
	ALLEGRO_FILEMODE_ISDIR   = 1 << 5
};

extern ALLEGRO_FS_ENTRY *al_create_fs_entry(const char *path);
extern void al_destroy_fs_entry(ALLEGRO_FS_ENTRY *e);
extern bool al_open_directory(ALLEGRO_FS_ENTRY *e);
extern ALLEGRO_FS_ENTRY *al_read_directory(ALLEGRO_FS_ENTRY *e);
extern bool al_close_directory(ALLEGRO_FS_ENTRY *e);
extern const char *al_get_fs_entry_name(ALLEGRO_FS_ENTRY *e);
extern uint32_t al_get_fs_entry_mode(ALLEGRO_FS_ENTRY *e);
extern bool al_make_directory(const char *path);

/* Files: only reading from memory (allegro_memfile.h). */
typedef struct ALLEGRO_FILE ALLEGRO_FILE;

extern int64_t al_fsize(ALLEGRO_FILE *f);
extern size_t al_fread(ALLEGRO_FILE *f, void *ptr, size_t size);
extern bool al_fclose(ALLEGRO_FILE *f);

/* Events: the menus wait for input or a timer tick. */
typedef struct ALLEGRO_EVENT_QUEUE ALLEGRO_EVENT_QUEUE;

enum {
	ALLEGRO_EVENT_TIMER     = 30,
	ALLEGRO_EVENT_DOS_INPUT = 1024      /* Keyboard or mouse: see input_dos.c. */
};

typedef union ALLEGRO_EVENT {
	unsigned int type;
} ALLEGRO_EVENT;

extern void al_wait_for_event(ALLEGRO_EVENT_QUEUE *queue, ALLEGRO_EVENT *ret_event);
extern void al_flush_event_queue(ALLEGRO_EVENT_QUEUE *queue);

/* Drawing: only clearing the screen. */
typedef struct ALLEGRO_COLOR {
	float r, g, b, a;
} ALLEGRO_COLOR;

extern ALLEGRO_COLOR al_map_rgb(unsigned char r, unsigned char g, unsigned char b);
extern void al_clear_to_color(ALLEGRO_COLOR color);

#ifdef __cplusplus
}
#endif

#endif
