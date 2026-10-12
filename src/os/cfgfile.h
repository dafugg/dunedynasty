/** @file src/os/cfgfile.h OS-independent INI-style configuration files. */

#ifndef OS_CFGFILE_H
#define OS_CFGFILE_H

#include <stdbool.h>

typedef struct CfgFile CfgFile;

extern CfgFile *CfgFile_Create(void);
extern CfgFile *CfgFile_Load(const char *filename);
extern bool CfgFile_Save(const char *filename, const CfgFile *cfg);
extern void CfgFile_Destroy(CfgFile *cfg);

extern const char *CfgFile_Get(const CfgFile *cfg, const char *section, const char *key);
extern void CfgFile_Set(CfgFile *cfg, const char *section, const char *key, const char *value);
extern void CfgFile_AddComment(CfgFile *cfg, const char *section, const char *comment);

#endif
