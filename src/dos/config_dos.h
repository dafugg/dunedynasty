/** @file src/dos/config_dos.h MS-DOS configuration additions. */

#ifndef DOS_CONFIG_DOS_H
#define DOS_CONFIG_DOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* [audio] voices: Sound Blaster voices mixed in software (1 to 12). */
extern int g_dos_sound_voices;
/* [audio] mixer_quality: Allegro mixer quality 0 (fastest) to 2, 3 = automatic. */
extern int g_dos_mixer_quality;

/* Command line overrides (-res, -scale, -voices, -quality, -allaudio)
 * apply to this run only.  Apply after loading the options; swap them
 * out before saving and back in afterwards. */
extern void ConfigDOS_ApplyCommandLine(void);
extern void ConfigDOS_SwapCommandLineOptions(void);

#ifdef __cplusplus
}
#endif

#endif
