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

| **Game slows down with the frame rate.** `GameLoop_ProcessGameTimer` jumped game time to the timer, so ticks between slow frames were lost (46% speed at 27 fps on a 486). Also, the loop drew only when the timer queue was empty, so a machine too slow for the game speed never drew. | `src/gameloop.c` | One step per tick, catching up on queued ticks, skipping ahead past 8 behind; draw at least every 6 ticks. Above 60 fps nothing changes. | Not submitted |

## Bugs found, not fixed

| Bug | Where | Notes |
|---|---|---|
| **Per-frame game-state work in the draw path.** `GUI_DrawInterfaceAndRadar` (now the every-frame draw) still runs `Unit_UpdateMap(1, u)` for every unit, as the original's occasional full refresh did: fog removal in each unit's radius, tile marking, visible-unit counts. 4.9 ms per frame on a 486DX2/66 in Atreides mission 3 (the structure loop was already disabled with `#if 0`). `Map_Client_UpdateFogOfWar` also copies all 4,096 tiles every frame (1.9 ms). Both make game state depend on frame rate. | `src/gui/gui.c` `GUI_DrawInterfaceAndRadar`, `src/map.c` `Map_Client_UpdateFogOfWar` | Cannot simply be skipped: these calls are how stationary units notice enemies entering sight range (`Map_UnveilTile` marks units/structures seen, wakes the AI). Measured cost: 5.5 ms per frame (3.3 ms `Map_UpdateAround`, 1.9 ms sight radius). Idea, **deferred** (changes spotting latency slightly): run both at a fixed 15 Hz (every 4 game ticks) regardless of frame rate, which also makes behaviour identical on every machine. |
| Hint popups redraw and shade the whole screen every frame (~9 ms per frame on a 486DX2/66) while the game is paused behind them. | `GameLoop_Client_Draw`, `MenuBar_DrawInGameOverlay` | Draw once while paused. |
| `Shape_Width`/`Shape_Height` fallbacks look swapped. | `src/shape.c:36-51` | Not yet confirmed to matter in play. |
| Unreachable 3440/3840 resolution branches. | `src/gfx.c:61-77` | Dead code; earlier conditions catch those widths. |

## Features that could be offered upstream

| Feature | Where | Notes |
|---|---|---|
| Config file reader/writer without Allegro 5 (`al_config`). | `src/os/cfgfile.c/.h`, `src/config_a5.c` | Now used on all platforms; removes an Allegro 5 dependency from config handling. |
| Directory listing and creation helpers (replace `al_make_directory`, `al_*_fs_entry`). | `src/os/fs.c/.h`, `src/file.c`, `src/newui/savemenu.c`, `src/newui/menu.c` | Same: less Allegro 5 outside the platform layer. |
| Build without ENet: `Net_IsAvailable()`, `src/net/net_none.c` keeps the single-player message path, lobby hides multiplayer. | `src/net/*`, `src/newui/menu_lobby.c` | Useful for minimal or embedded builds. |
| Compact build list option (off by default): two columns of small icons, as many rows as fit, like Command & Conquer; READY, percentage and pause sign drawn on the icons. Any resolution; 4 items instead of 1 at 320x240, all 7 of a mission 3 yard at 640x480. | `src/newui/actionpanel.c`, `src/opendune.c` (widget width), `src/enhancement.c`, `src/config_a5.c`, `src/newui/menu_extras.c` | Screenshots in the share's `compact-build-list/`. |
| `InputA5_WaitForEvent` / `InputA5_FlushEvents` (block instead of polling). | `src/input/input_a5.c` | Small; check whether upstream wants it on its own. |

## Feature ideas from other remakes

From a survey of Dune II – The Maker (MIT) and Dune Legacy (GPL-2.0+).
Both licences are compatible, but both are C++ engines with their own
structures, so expect to reimplement ideas rather than copy code.
Starred (★) items are the most promising.

| Idea | Notes | Machines |
|---|---|---|
| ★ **Better pathfinding** | Dune Dynasty still uses Dune II's pathfinder (`src/script/unit.c:1229`): head for the target, trace round obstacles both ways, give up after 40 steps, keep 14. Hence units stuck in rock pockets and shuffling along cliffs. Add a breadth-first or A* search as an `enhancement_*` option, writing the same 14-step `route[]`. Limit searches per game tick; share one distance field when a group is sent to one place (D2TM uses a distance field, `cPathFinder.cpp`). Cost on a 486 unmeasured; maps are at most 64×64. | All |
| ★ **Attack-move** | Move, but stop to fight enemies met on the way. Possibly Shift+click queued orders too. | All |
| ★ **More skirmish options** | "Surprise me" (D2TM `cSetupSkirmishState.cpp:1247`: random map or seed, houses, opponents, credits, then start). Check which lobby settings Dune Dynasty already has before adding more. | All |
| ★ **Better guard response** | Guard (mode 1 in `Unit_FindBestTargetUnit`, `src/unit.c:1227`) only targets enemies within the unit's own weapon range of where it stands; Area Guard doubles that around its origin. A guarding unit ignores a neighbour under fire just out of range. Option: also react to attacks on friendly units or buildings within a short radius, then return to its post. | All |
| ★ **AI that defends itself better, fairly** | Nearby units respond when the base is attacked (Dune Legacy's QuantBot does this). Must use only what a human player could see and do: no map knowledge through fog, no extra resources. | All; heavier versions Pentium |
| Camera follows selected units | D2TM's T key. Dune Dynasty lists follow/escort as a wish (`enhancement.txt:257`). | All |
| AI expansion bases | Dune Legacy: AI builds on free rock with escorts. | Pentium |
| Options for faster CPUs only | 6-player brutal AI and raised unit caps (Pentium); smooth unit rotation, high-resolution overlays, 800x600 and up, MP3 music (Pentium MMX and up). Defaults could follow the CPU detected at start-up. Until the game-speed fix, a heavy option costs game speed, not just frames. | Tiered |
| Bigger maps | Not practical: 64×64 is built into the 6-bit tile coordinates. | — |

### Save games

Every new feature must keep save games working:

- **Old saves load in new builds, and options can change between saving
  and loading.** A move in progress is only `targetMove` plus the 14-step
  `route[]` (saved in the `UNIT` chunk, `src/saveload/unit.c`). A new
  pathfinder must write the same `route[]` format, so a saved route just
  continues and the next re-search uses whichever pathfinder is selected.
- **No changes to existing chunks.** Save files are chunked, and the
  loader skips chunks it doesn't know (`src/load.c:174`); Dune Dynasty
  already adds its own (`DDAI`, `DDU2`, ...). Prefer features with no
  saved state at all: pathfinding, guard response and AI defence can be
  recomputed after loading.
- **If a feature needs new saved state** (e.g. attack-move remembering
  that a unit is attack-moving), put it in a new optional chunk, and
  make loading without it fall back to plain behaviour (an ordinary
  move). Older builds then skip the chunk and still load the save.
- **Multiplayer**: check whether all machines must compute identical
  paths before changing the pathfinder; not yet checked in the net code.

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

