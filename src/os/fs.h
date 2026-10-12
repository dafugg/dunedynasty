/** @file src/os/fs.h OS-independent directory routines. */

#ifndef OS_FS_H
#define OS_FS_H

#include <stdbool.h>

typedef void (*FS_ListCallback)(const char *dirname, const char *name, bool is_directory, void *data);

extern bool FS_MakeDirectory(const char *path);
extern bool FS_ListDirectory(const char *dirname, FS_ListCallback callback, void *data);

#endif
