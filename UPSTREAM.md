# Upstream candidates (gameflorist/dunedynasty)

Bugs and features from the MS-DOS port work that could go back to
https://github.com/gameflorist/dunedynasty as issues or pull requests,
including the MS-DOS build target itself.

Keep this up to date as things are found or fixed.

## Bugs found and fixed here

| Bug | Where | Fix | PR status |
|---|---|---|---|
| **Jukebox crash when a track ends.** Auto-advance moves to the next music group. *Idle (Others)* (after *Idle 9*) lists only external music packs; when none are installed, the fallback looks for a Dune II AdLib track that the group does not have, and passes a NULL track to `Audio_PlayMusicFile` (page fault). Found on DOS (no external packs); affects any install without them. | `src/audio/audio.c` `Audio_PlayMusicGroup`, `Audio_PlayMusicNextInSequence` | Return if no track is found; skip groups with no playable songs when advancing. | Not submitted |
| `Audio_ScanMusic` resets `count` and `count_default` but not `count_found`, so a second scan would double-count. Latent: only scanned once today. | `src/audio/audio.c` `Audio_ScanMusic` | Reset `count_found` too. | Not submitted |
| `AdLibDriver::callback` returns early on an unknown opcode without calling `unlock()`. Harmless where lock/unlock are no-ops (desktop); deadlocks or leaves interrupts off where they are real. | `src/audio/adl/sound_adlib.cpp` | `unlock()` before the early return. | Not submitted |

## Bugs found, not fixed

| Bug | Where | Notes |
|---|---|---|
| **Game speed tied to frame rate.** Game logic runs at most once per frame: queued game-timer ticks find `g_timerGame == Timer_GameTicks()` and return. Below the game speed's tick rate (60 Hz at normal) the game slows down, e.g. ~70% at 42 fps. Invisible on desktop (always 60 fps+). | `src/gameloop.c` `GameLoop_ProcessGameTimer` | Fix: run each elapsed step before drawing, capped. Changes game feel on slow machines; needs a decision (and care with multiplayer sync). |
| `Shape_Width`/`Shape_Height` fallbacks look swapped. | `src/shape.c:36-51` | Not yet confirmed to matter in play. |
| Unreachable 3440/3840 resolution branches. | `src/gfx.c:61-77` | Dead code; earlier conditions catch those widths. |

## Features that could be offered upstream

| Feature | Where | Notes |
|---|---|---|
| Config file reader/writer without Allegro 5 (`al_config`). | `src/os/cfgfile.c/.h`, `src/config_a5.c` | Now used on all platforms; removes an Allegro 5 dependency from config handling. |
| Directory listing and creation helpers (replace `al_make_directory`, `al_*_fs_entry`). | `src/os/fs.c/.h`, `src/file.c`, `src/newui/savemenu.c`, `src/newui/menu.c` | Same: less Allegro 5 outside the platform layer. |
| Build without ENet: `Net_IsAvailable()`, `src/net/net_none.c` keeps the single-player message path, lobby hides multiplayer. | `src/net/*`, `src/newui/menu_lobby.c` | Useful for minimal or embedded builds. |
| `InputA5_WaitForEvent` / `InputA5_FlushEvents` (block instead of polling). | `src/input/input_a5.c` | Small; check whether upstream wants it on its own. |

## MS-DOS 6.22 as a build target

Offer the DOS port as a target alongside upstream's Linux, macOS and
Windows builds, without changing those builds.

What it adds:

- `cmake/djgpp.cmake` toolchain file and an `MSDOS` branch in
  `CMakeLists.txt` / `cmake/FileList.cmake` (no ENet, codecs, FluidSynth).
- Platform layer on stock Allegro 4.2 (the last Allegro with DOS
  support): `src/*_dos.*`, `src/video/video_dos.c`, `prim_dos.c`,
  `src/input/input_dos.c`, `src/timer/timer_dos.c`, `perf_dos.c`,
  `src/audio/audio_dos.cpp`, `midi_dos.c`, `adl/opl_hw.cpp`,
  `src/sysinfo_dos.c`, `src/crashlog/errorlog_dos.c`.
- `scripts/dos/setup-toolchain.sh` (DJGPP + Allegro 4.2, no root) and
  `scripts/dos/package.sh` (DUNEDYN.EXE, CWSDPMI, README, BAT presets).
- `__DJGPP__` branches in shared files (`gameloop.c` profiling hooks,
  `config_a5.c` DOS keys and command-line overrides, `sound_adlib.cpp`
  interrupt-safe locking and memory loading, `file.c`).

Suggested PR order:

1. The portable pieces above (config/fs helpers, build without ENet,
   bug fixes). Useful on their own and shrink the DOS PR.
2. The DOS target, plus a `dos-djgpp-build.yml` workflow matching the
   existing per-platform workflows (cache DJGPP and Allegro 4.2).

Before submitting:

- Real-hardware testing (486 to Pentium III) and the open DOS items:
  8 MB machines, save/load, Dune Dynasty's extra controls on DOS.
- Ask upstream whether they want it, and who maintains it.
- Licences: Allegro 4 (giftware), CWSDPMI (redistributable with its
  docs), DJGPP runtime. Ship stock binaries and sources only.
- Decide whether the 86Box/DOSBox-X test scripts go upstream. They need
  user-supplied MS-DOS 6.22 disks and BIOS ROMs, so maybe as optional
  developer tooling with docs only.

