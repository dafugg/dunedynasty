/**
 * @file src/crashlog/errorlog_dos.c
 *
 * MS-DOS run log and error messages.  stderr is lost once a graphics
 * mode is set and COMMAND.COM cannot redirect it, so everything goes to
 * DUNEDYN.LOG beside the executable.
 */

#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "errorlog.h"
#include "runlog_dos.h"
#include "types.h"
#include "../timer/perf_dos.h"

/* DUNEDYN.OLD keeps a few previous runs; start it afresh past this. */
#define RUNLOG_OLD_MAX      (256 * 1024L)

/* Stop logging warnings that repeat every frame. */
#define RUNLOG_MAX_WARNINGS 200

static FILE *s_logfile;
static bool s_graphics;
static bool s_echo_stdout;
static int s_errors_in_graphics;
static int s_warnings;

static void
RunLog_MakePath(char *buf, size_t size, const char *argv0, const char *name)
{
	snprintf(buf, size, "%s", (argv0 != NULL) ? argv0 : "");

	char *slash = strrchr(buf, '/');
	char *bslash = strrchr(buf, '\\');
	if (bslash > slash)
		slash = bslash;

	if (slash != NULL) {
		slash[1] = '\0';
	} else {
		buf[0] = '\0';
	}

	strncat(buf, name, size - strlen(buf) - 1);
}

/* Move the previous log to the end of DUNEDYN.OLD. */
static void
RunLog_KeepPrevious(const char *log, const char *old)
{
	FILE *in = fopen(log, "rb");
	if (in == NULL)
		return;

	struct stat st;
	const bool too_big = (stat(old, &st) == 0 && st.st_size > RUNLOG_OLD_MAX);
	FILE *out = fopen(old, too_big ? "wb" : "ab");

	if (out != NULL) {
		char buf[4096];
		size_t n;

		while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
			fwrite(buf, 1, n, out);

		fputs("\r\n", out);
		fclose(out);
	}

	fclose(in);
}

void
RunLog_Open(const char *argv0)
{
	char log[1024], old[1024];

	RunLog_MakePath(log, sizeof(log), argv0, "DUNEDYN.LOG");
	RunLog_MakePath(old, sizeof(old), argv0, "DUNEDYN.OLD");

	RunLog_KeepPrevious(log, old);

	s_logfile = fopen(log, "w");
	s_echo_stdout = !isatty(fileno(stdout));
}

void
RunLog_Close(void)
{
	if (s_logfile != NULL)
		fclose(s_logfile);
	s_logfile = NULL;
}

void
RunLog_SetGraphicsMode(bool graphics)
{
	s_graphics = graphics;

	if (!graphics && s_errors_in_graphics > 0) {
		fprintf(stderr, "%d error%s logged in DUNEDYN.LOG.\n",
			s_errors_in_graphics, (s_errors_in_graphics == 1) ? "" : "s");
		s_errors_in_graphics = 0;
	}
}

static void
RunLog_Write(const char *prefix, const char *format, va_list ap)
{
	if (s_logfile == NULL)
		return;

	const double secs = (double)Perf_Now() / PERF_TICKS_PER_SECOND;

	fprintf(s_logfile, "%8.2f %s", secs, prefix);
	vfprintf(s_logfile, format, ap);

	/* Commit to disk: a hang that needs a reset must not lose the log. */
	fflush(s_logfile);
	fsync(fileno(s_logfile));
}

void
RunLog_Printf(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	RunLog_Write("", format, ap);
	va_end(ap);

	if (s_echo_stdout) {
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
	}
}

static void
ErrorLog_Write(const char *prefix, const char *format, va_list ap)
{
	va_list ap2;

	va_copy(ap2, ap);
	RunLog_Write(prefix, format, ap2);
	va_end(ap2);

	if (!s_graphics) {
		vfprintf(stderr, format, ap);
	} else if (prefix[0] == 'E') {
		s_errors_in_graphics++;
	}
}

/* The log is opened by A5_InitSystem, before the data directories are known. */
void
ErrorLog_Init(const char *dir)
{
	VARIABLE_NOT_USED(dir);
}

void
Error(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	ErrorLog_Write("Error: ", format, ap);
	va_end(ap);
}

void
Warning(const char *format, ...)
{
	va_list ap;

	if (++s_warnings > RUNLOG_MAX_WARNINGS) {
		if (s_warnings == RUNLOG_MAX_WARNINGS + 1)
			RunLog_Printf("Too many warnings; no more will be logged.\n");
		return;
	}

	va_start(ap, format);
	ErrorLog_Write("Warning: ", format, ap);
	va_end(ap);
}
