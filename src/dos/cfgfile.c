/** @file src/dos/cfgfile.c OS-independent INI-style configuration files.
 *
 * Mirrors the subset of Allegro 5's config API that Dune Dynasty uses:
 * comments and entry order are preserved on save, entries before the
 * first [section] header belong to the global section (NULL or ""),
 * and setting a missing key appends it to its section.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfgfile.h"

typedef struct CfgEntry {
	char *key;      /* NULL for comments and blank lines. */
	char *value;    /* Value, or comment text. */
	struct CfgEntry *next;
} CfgEntry;

typedef struct CfgSection {
	char *name;     /* "" for the global section. */
	CfgEntry *head;
	CfgEntry *tail;
	struct CfgSection *next;
} CfgSection;

struct CfgFile {
	CfgSection *head;
	CfgSection *tail;
};

static char *
CfgFile_StrDup(const char *str, size_t len)
{
	char *dup = malloc(len + 1);
	if (dup != NULL) {
		memcpy(dup, str, len);
		dup[len] = '\0';
	}
	return dup;
}

/* Duplicate [begin, end) with surrounding whitespace removed. */
static char *
CfgFile_StrDupTrim(const char *begin, const char *end)
{
	while (begin < end && isspace((unsigned char)*begin))
		begin++;
	while (end > begin && isspace((unsigned char)end[-1]))
		end--;
	return CfgFile_StrDup(begin, end - begin);
}

static CfgSection *
CfgFile_FindSection(const CfgFile *cfg, const char *name)
{
	if (name == NULL)
		name = "";

	for (CfgSection *sec = cfg->head; sec != NULL; sec = sec->next) {
		if (strcmp(sec->name, name) == 0)
			return sec;
	}

	return NULL;
}

static CfgSection *
CfgFile_GetSection(CfgFile *cfg, const char *name)
{
	if (name == NULL)
		name = "";

	CfgSection *sec = CfgFile_FindSection(cfg, name);
	if (sec != NULL)
		return sec;

	sec = calloc(1, sizeof(*sec));
	if (sec == NULL)
		return NULL;

	sec->name = CfgFile_StrDup(name, strlen(name));

	/* The global section always comes first. */
	if (name[0] == '\0') {
		sec->next = cfg->head;
		cfg->head = sec;
		if (cfg->tail == NULL)
			cfg->tail = sec;
	} else {
		if (cfg->tail != NULL) {
			cfg->tail->next = sec;
		} else {
			cfg->head = sec;
		}
		cfg->tail = sec;
	}

	return sec;
}

static void
CfgFile_Append(CfgSection *sec, char *key, char *value)
{
	CfgEntry *e = calloc(1, sizeof(*e));
	if (e == NULL) {
		free(key);
		free(value);
		return;
	}

	e->key = key;
	e->value = value;

	if (sec->tail != NULL) {
		sec->tail->next = e;
	} else {
		sec->head = e;
	}
	sec->tail = e;
}

CfgFile *
CfgFile_Create(void)
{
	CfgFile *cfg = calloc(1, sizeof(*cfg));
	if (cfg == NULL)
		return NULL;

	CfgFile_GetSection(cfg, NULL);
	return cfg;
}

CfgFile *
CfgFile_Load(const char *filename)
{
	FILE *fp = fopen(filename, "r");
	if (fp == NULL)
		return NULL;

	CfgFile *cfg = CfgFile_Create();
	if (cfg == NULL) {
		fclose(fp);
		return NULL;
	}

	CfgSection *sec = cfg->head;
	char line[1024];

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *end = line + strlen(line);
		while (end > line && (end[-1] == '\n' || end[-1] == '\r'))
			*(--end) = '\0';

		const char *p = line;
		while (isspace((unsigned char)*p))
			p++;

		if (*p == '[') {
			const char *close = strchr(p, ']');
			if (close != NULL) {
				char *name = CfgFile_StrDupTrim(p + 1, close);
				if (name != NULL) {
					sec = CfgFile_GetSection(cfg, name);
					free(name);
				}
				if (sec == NULL)
					sec = cfg->head;
				continue;
			}
		}

		const char *eq = strchr(p, '=');
		if (*p == '#' || *p == '\0' || eq == NULL) {
			CfgFile_Append(sec, NULL, CfgFile_StrDup(line, end - line));
		} else {
			CfgFile_Append(sec, CfgFile_StrDupTrim(p, eq), CfgFile_StrDupTrim(eq + 1, end));
		}
	}

	fclose(fp);
	return cfg;
}

bool
CfgFile_Save(const char *filename, const CfgFile *cfg)
{
	FILE *fp = fopen(filename, "w");
	if (fp == NULL)
		return false;

	for (const CfgSection *sec = cfg->head; sec != NULL; sec = sec->next) {
		if (sec->name[0] != '\0')
			fprintf(fp, "[%s]\n", sec->name);

		for (const CfgEntry *e = sec->head; e != NULL; e = e->next) {
			if (e->key == NULL) {
				fprintf(fp, "%s\n", e->value);
			} else {
				fprintf(fp, "%s=%s\n", e->key, e->value);
			}
		}
	}

	const bool ok = (ferror(fp) == 0);
	return (fclose(fp) == 0) && ok;
}

void
CfgFile_Destroy(CfgFile *cfg)
{
	if (cfg == NULL)
		return;

	CfgSection *sec = cfg->head;
	while (sec != NULL) {
		CfgSection *next_sec = sec->next;

		CfgEntry *e = sec->head;
		while (e != NULL) {
			CfgEntry *next = e->next;
			free(e->key);
			free(e->value);
			free(e);
			e = next;
		}

		free(sec->name);
		free(sec);
		sec = next_sec;
	}

	free(cfg);
}

const char *
CfgFile_Get(const CfgFile *cfg, const char *section, const char *key)
{
	if (cfg == NULL)
		return NULL;

	const CfgSection *sec = CfgFile_FindSection(cfg, section);
	if (sec == NULL)
		return NULL;

	for (const CfgEntry *e = sec->head; e != NULL; e = e->next) {
		if (e->key != NULL && strcmp(e->key, key) == 0)
			return e->value;
	}

	return NULL;
}

void
CfgFile_Set(CfgFile *cfg, const char *section, const char *key, const char *value)
{
	if (cfg == NULL)
		return;

	CfgSection *sec = CfgFile_GetSection(cfg, section);
	if (sec == NULL)
		return;

	for (CfgEntry *e = sec->head; e != NULL; e = e->next) {
		if (e->key != NULL && strcmp(e->key, key) == 0) {
			char *dup = CfgFile_StrDup(value, strlen(value));
			if (dup != NULL) {
				free(e->value);
				e->value = dup;
			}
			return;
		}
	}

	CfgFile_Append(sec, CfgFile_StrDup(key, strlen(key)), CfgFile_StrDup(value, strlen(value)));
}

void
CfgFile_AddComment(CfgFile *cfg, const char *section, const char *comment)
{
	if (cfg == NULL)
		return;

	CfgSection *sec = CfgFile_GetSection(cfg, section);
	if (sec == NULL)
		return;

	CfgFile_Append(sec, NULL, CfgFile_StrDup(comment, strlen(comment)));
}
