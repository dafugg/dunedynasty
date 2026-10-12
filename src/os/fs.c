/** @file src/os/fs.c OS-independent directory routines. */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fs.h"

static bool
FS_IsDirectory(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

static bool
FS_MakeOneDirectory(const char *path)
{
#if defined(_WIN32)
	if (mkdir(path) == 0)
		return true;
#else
	if (mkdir(path, 0755) == 0)
		return true;
#endif

	return (errno == EEXIST) && FS_IsDirectory(path);
}

/**
 * Create a directory, including any missing parent directories.
 * @return true if the directory exists afterwards.
 */
bool
FS_MakeDirectory(const char *path)
{
	char buf[1024];
	size_t len = strlen(path);

	if (len == 0 || len >= sizeof(buf))
		return false;

	memcpy(buf, path, len + 1);

	/* Strip trailing separators. */
	while (len > 1 && (buf[len - 1] == '/' || buf[len - 1] == '\\'))
		buf[--len] = '\0';

	if (FS_IsDirectory(buf))
		return true;

	for (char *p = buf + 1; *p != '\0'; p++) {
		if (*p != '/' && *p != '\\')
			continue;

		/* Skip drive specifiers, e.g. "C:\". */
		if (p[-1] == ':')
			continue;

		const char c = *p;
		*p = '\0';
		if (!FS_IsDirectory(buf) && !FS_MakeOneDirectory(buf))
			return false;
		*p = c;
	}

	return FS_MakeOneDirectory(buf);
}

/**
 * Call callback for each entry in dirname, excluding "." and "..".
 * @return false if the directory could not be opened.
 */
bool
FS_ListDirectory(const char *dirname, FS_ListCallback callback, void *data)
{
	DIR *dir = opendir(dirname);
	if (dir == NULL)
		return false;

	const size_t len = strlen(dirname);
	const char *sep = (len > 0 && (dirname[len - 1] == '/' || dirname[len - 1] == '\\')) ? "" : "/";

	struct dirent *de;
	while ((de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		char path[1024];
		snprintf(path, sizeof(path), "%s%s%s", dirname, sep, de->d_name);
		callback(dirname, de->d_name, FS_IsDirectory(path), data);
	}

	closedir(dir);
	return true;
}
