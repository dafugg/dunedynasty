/** @file src/dos/a5compat.c
 *
 * MS-DOS: the Allegro 5 calls used by Dune Dynasty's shared code, on
 * DJGPP (see a5compat/allegro5/allegro.h).  This keeps the shared code
 * identical to the desktop builds instead of adding wrappers or
 * #ifdefs at each call site.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <allegro5/allegro.h>

#include "cfgfile.h"
#include "../input/input_a5.h"
#include "../input/input_dos.h"
#include "../timer/timer_dos.h"
#include "../video/video_dos.h"

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

/*--------------------------------------------------------------*/
/* Configuration files: ALLEGRO_CONFIG is a CfgFile. */

ALLEGRO_CONFIG *
al_create_config(void)
{
	return (ALLEGRO_CONFIG *)CfgFile_Create();
}

ALLEGRO_CONFIG *
al_load_config_file(const char *filename)
{
	return (ALLEGRO_CONFIG *)CfgFile_Load(filename);
}

bool
al_save_config_file(const char *filename, const ALLEGRO_CONFIG *config)
{
	return CfgFile_Save(filename, (const CfgFile *)config);
}

void
al_destroy_config(ALLEGRO_CONFIG *config)
{
	CfgFile_Destroy((CfgFile *)config);
}

const char *
al_get_config_value(const ALLEGRO_CONFIG *config, const char *section, const char *key)
{
	return CfgFile_Get((const CfgFile *)config, section, key);
}

void
al_set_config_value(ALLEGRO_CONFIG *config, const char *section, const char *key, const char *value)
{
	CfgFile_Set((CfgFile *)config, section, key, value);
}

void
al_add_config_comment(ALLEGRO_CONFIG *config, const char *section, const char *comment)
{
	CfgFile_AddComment((CfgFile *)config, section, comment);
}

/*--------------------------------------------------------------*/
/* Paths: a directory ending in '/' and a file name. */

struct ALLEGRO_PATH {
	char dir[PATH_MAX];
	char filename[PATH_MAX];
	char buf[2 * PATH_MAX];         /* al_path_cstr */
	char part[PATH_MAX];            /* al_get_path_tail, _basename */
};

static void
A5Compat_Slashes(char *s)
{
	for (; *s != '\0'; s++) {
		if (*s == '\\')
			*s = '/';
	}
}

static ALLEGRO_PATH *
A5Compat_NewPath(void)
{
	return calloc(1, sizeof(ALLEGRO_PATH));
}

ALLEGRO_PATH *
al_create_path(const char *str)
{
	ALLEGRO_PATH *path = A5Compat_NewPath();
	if (path == NULL)
		return NULL;

	char tmp[PATH_MAX];
	snprintf(tmp, sizeof(tmp), "%s", (str != NULL) ? str : "");
	A5Compat_Slashes(tmp);

	char *slash = strrchr(tmp, '/');
	if (slash != NULL) {
		snprintf(path->filename, sizeof(path->filename), "%s", slash + 1);
		slash[1] = '\0';
		snprintf(path->dir, sizeof(path->dir), "%s", tmp);
	} else {
		snprintf(path->filename, sizeof(path->filename), "%s", tmp);
	}

	return path;
}

ALLEGRO_PATH *
al_create_path_for_directory(const char *str)
{
	ALLEGRO_PATH *path = A5Compat_NewPath();
	if (path == NULL)
		return NULL;

	snprintf(path->dir, sizeof(path->dir), "%s", (str != NULL) ? str : "");
	A5Compat_Slashes(path->dir);

	const size_t len = strlen(path->dir);
	if (len > 0 && path->dir[len - 1] != '/' && len + 1 < sizeof(path->dir))
		strcat(path->dir, "/");

	return path;
}

/* Everything lives next to the executable on DOS. */
ALLEGRO_PATH *
al_get_standard_path(int id)
{
	extern char **__crt0_argv;
	ALLEGRO_PATH *exe = al_create_path(__crt0_argv[0]);

	if (exe != NULL && id != ALLEGRO_EXENAME_PATH) {
		exe->filename[0] = '\0';
		if (exe->dir[0] == '\0')
			snprintf(exe->dir, sizeof(exe->dir), "./");
	}

	return exe;
}

void
al_destroy_path(ALLEGRO_PATH *path)
{
	free(path);
}

const char *
al_path_cstr(const ALLEGRO_PATH *path, char delim)
{
	ALLEGRO_PATH *p = (ALLEGRO_PATH *)path;

	snprintf(p->buf, sizeof(p->buf), "%s%s", p->dir, p->filename);
	if (delim != '/') {
		for (char *c = p->buf; *c != '\0'; c++) {
			if (*c == '/')
				*c = delim;
		}
	}

	return p->buf;
}

/* The last directory component. */
const char *
al_get_path_tail(const ALLEGRO_PATH *path)
{
	ALLEGRO_PATH *p = (ALLEGRO_PATH *)path;

	snprintf(p->part, sizeof(p->part), "%s", p->dir);

	size_t len = strlen(p->part);
	if (len > 0 && p->part[len - 1] == '/')
		p->part[--len] = '\0';

	const char *slash = strrchr(p->part, '/');
	return (slash != NULL) ? slash + 1 : p->part;
}

const char *
al_get_path_filename(const ALLEGRO_PATH *path)
{
	return path->filename;
}

const char *
al_get_path_extension(const ALLEGRO_PATH *path)
{
	const char *dot = strrchr(path->filename, '.');
	return (dot != NULL) ? dot : "";
}

const char *
al_get_path_basename(const ALLEGRO_PATH *path)
{
	ALLEGRO_PATH *p = (ALLEGRO_PATH *)path;

	snprintf(p->part, sizeof(p->part), "%s", p->filename);

	char *dot = strrchr(p->part, '.');
	if (dot != NULL)
		*dot = '\0';

	return p->part;
}

bool
al_set_path_filename(ALLEGRO_PATH *path, const char *filename)
{
	snprintf(path->filename, sizeof(path->filename), "%s", (filename != NULL) ? filename : "");
	return true;
}

/* Only the last component (i == -1) is used, for "bin" -> "share/...". */
void
al_replace_path_component(ALLEGRO_PATH *path, int i, const char *s)
{
	if (i != -1)
		return;

	size_t len = strlen(path->dir);
	if (len > 0 && path->dir[len - 1] == '/')
		path->dir[--len] = '\0';

	char *slash = strrchr(path->dir, '/');
	if (slash != NULL) {
		slash[1] = '\0';
	} else {
		path->dir[0] = '\0';
	}

	strncat(path->dir, s, sizeof(path->dir) - strlen(path->dir) - 2);
	strcat(path->dir, "/");
}

/*--------------------------------------------------------------*/
/* Directories. */

struct ALLEGRO_FS_ENTRY {
	char name[PATH_MAX];
	DIR *dir;
};

ALLEGRO_FS_ENTRY *
al_create_fs_entry(const char *path)
{
	ALLEGRO_FS_ENTRY *e = calloc(1, sizeof(ALLEGRO_FS_ENTRY));
	if (e != NULL)
		snprintf(e->name, sizeof(e->name), "%s", path);
	return e;
}

void
al_destroy_fs_entry(ALLEGRO_FS_ENTRY *e)
{
	if (e != NULL && e->dir != NULL)
		closedir(e->dir);
	free(e);
}

bool
al_open_directory(ALLEGRO_FS_ENTRY *e)
{
	e->dir = opendir(e->name);
	return (e->dir != NULL);
}

ALLEGRO_FS_ENTRY *
al_read_directory(ALLEGRO_FS_ENTRY *e)
{
	struct dirent *d;

	while ((d = readdir(e->dir)) != NULL) {
		if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
			continue;

		ALLEGRO_FS_ENTRY *f = calloc(1, sizeof(ALLEGRO_FS_ENTRY));
		if (f == NULL)
			return NULL;

		const size_t len = strlen(e->name);
		const bool slash = (len > 0 && (e->name[len - 1] == '/' || e->name[len - 1] == '\\'));
		snprintf(f->name, sizeof(f->name), "%s%s%s", e->name, slash ? "" : "/", d->d_name);
		return f;
	}

	return NULL;
}

bool
al_close_directory(ALLEGRO_FS_ENTRY *e)
{
	if (e->dir != NULL)
		closedir(e->dir);
	e->dir = NULL;
	return true;
}

const char *
al_get_fs_entry_name(ALLEGRO_FS_ENTRY *e)
{
	return e->name;
}

uint32_t
al_get_fs_entry_mode(ALLEGRO_FS_ENTRY *e)
{
	struct stat st;

	if (stat(e->name, &st) != 0)
		return 0;

	return ALLEGRO_FILEMODE_READ | (S_ISDIR(st.st_mode) ? ALLEGRO_FILEMODE_ISDIR : ALLEGRO_FILEMODE_ISFILE);
}

static bool
A5Compat_IsDirectory(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

/* Create a directory and any missing parents, as Allegro 5 does. */
bool
al_make_directory(const char *path)
{
	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "%s", path);
	A5Compat_Slashes(buf);

	size_t len = strlen(buf);
	while (len > 1 && buf[len - 1] == '/')
		buf[--len] = '\0';

	if (len == 0 || A5Compat_IsDirectory(buf))
		return true;

	for (char *c = buf + 1; *c != '\0'; c++) {
		if (*c != '/' || c[-1] == ':')
			continue;

		*c = '\0';
		if (!A5Compat_IsDirectory(buf) && mkdir(buf, 0755) != 0 && errno != EEXIST) {
			*c = '/';
			return false;
		}
		*c = '/';
	}

	return (mkdir(buf, 0755) == 0) || A5Compat_IsDirectory(buf);
}

/*--------------------------------------------------------------*/
/* Memory files (allegro_memfile.h), read only. */

#include <allegro5/allegro_memfile.h>

struct ALLEGRO_FILE {
	const unsigned char *mem;
	int64_t size;
	int64_t pos;
};

ALLEGRO_FILE *
al_open_memfile(void *mem, int64_t size, const char *mode)
{
	(void)mode;

	ALLEGRO_FILE *f = calloc(1, sizeof(ALLEGRO_FILE));
	if (f != NULL) {
		f->mem = mem;
		f->size = size;
	}
	return f;
}

int64_t
al_fsize(ALLEGRO_FILE *f)
{
	return f->size;
}

size_t
al_fread(ALLEGRO_FILE *f, void *ptr, size_t size)
{
	const int64_t left = f->size - f->pos;
	if ((int64_t)size > left)
		size = (size_t)left;

	memcpy(ptr, f->mem + f->pos, size);
	f->pos += size;
	return size;
}

bool
al_fclose(ALLEGRO_FILE *f)
{
	free(f);
	return true;
}

/*--------------------------------------------------------------*/
/* Events. */

struct ALLEGRO_EVENT_QUEUE *g_a5_input_queue;

void
al_wait_for_event(ALLEGRO_EVENT_QUEUE *queue, ALLEGRO_EVENT *ret_event)
{
	(void)queue;
	ret_event->type = InputDOS_WaitForEvent() ? ALLEGRO_EVENT_TIMER : ALLEGRO_EVENT_DOS_INPUT;
}

void
al_flush_event_queue(ALLEGRO_EVENT_QUEUE *queue)
{
	(void)queue;
	InputDOS_FlushEvents();
}

/*--------------------------------------------------------------*/
/* Drawing. */

ALLEGRO_COLOR
al_map_rgb(unsigned char r, unsigned char g, unsigned char b)
{
	const ALLEGRO_COLOR c = { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
	return c;
}

/* The shared code only clears to black. */
void
al_clear_to_color(ALLEGRO_COLOR color)
{
	(void)color;
	VideoDOS_ClearScreen();
}
