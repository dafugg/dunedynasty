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
| **F10 FPS counter invisible in game on the desktop build.** `VideoA5_Tick` draws it at (2, 40) in whatever transform was used last; in game that is a clipped sidebar/menubar area, so it only shows in menus and the mentat. Reported on Windows. | `src/video/video_a5.c` `VideoA5_Tick` | Switch to `SCREENDIV_MAIN` around the FPS drawing, as the software cursor code just below already does (2 lines). |
| `Shape_Width`/`Shape_Height` fallbacks look swapped. | `src/shape.c:36-51` | Not yet confirmed to matter in play. |
| Unreachable 3440/3840 resolution branches. | `src/gfx.c:61-77` | Dead code; earlier conditions catch those widths. |

## Features that could be offered upstream

| Feature | Where | Notes |
|---|---|---|
| Build without ENet: `Net_IsAvailable()`, `src/net/net_none.c` keeps the single-player message path, lobby hides multiplayer. | `src/net/*`, `src/newui/menu_lobby.c` | Useful for minimal or embedded builds. |
| Compact build list option (off by default): two columns of small icons, as many rows as fit, like Command & Conquer; READY, percentage and pause sign drawn on the icons. Any resolution; 4 items instead of 1 at 320x240, all 7 of a mission 3 yard at 640x480. | `src/newui/actionpanel.c`, `src/opendune.c` (widget width), `src/enhancement.c`, `src/config_a5.c`, `src/newui/menu_extras.c` | Screenshots in the share's `compact-build-list/`. |

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
Windows builds, without changing those builds. Everything DOS-specific
lives in new files (`src/dos/`, `src/*_dos.*`, `src/*/*_dos.*`,
`src/audio/adl/opl_hw.cpp`, `cmake/djgpp.cmake`, `scripts/dos/`);
changes to upstream's own files are kept to the hooks listed below.

### How DOS avoids changing upstream's Allegro 5 code

Upstream's shared code calls Allegro 5 directly for config files,
paths, directory listing, the menu's event wait and screen clear, and
the AdLib player's file loading: about 40 calls in `config_a5.c`,
`menu.c`, `savemenu.c`, `file.c`, `gameloop.c` and `sound_adlib.cpp`.
Allegro 5 has no DOS port (the DOS platform layer uses Allegro 4).

**What we did:** `src/dos/a5compat/` provides just those Allegro 5
calls, with Allegro 5's names and types, implemented for DJGPP
(`src/dos/a5compat.c`, using our own INI reader `src/dos/cfgfile.c`).
Only the DOS build puts it on the include path. The shared files keep
upstream's code, and the desktop builds still use real Allegro 5.

**What would have happened otherwise:** every call site would need a
wrapper function or an `#ifdef`. A first version did that: about 1,500
changed lines in upstream files, our own config reader and directory
code on every platform (changing desktop behaviour), and wrappers to
review in six files. With the compatibility layer those files are back
to upstream's code.

### Remaining changes to upstream files, by pull request

Line counts are approximate (added plus removed).

**1. Bug fixes** (independent of DOS)

| File | Change | Why | Without it |
|---|---|---|---|
| `src/audio/audio.c` (19) | Jukebox advance skips groups with nothing to play; return if no track is found; reset `count_found` when scanning | The next group (Idle (Others)) can have no installed songs | Crash when a jukebox track ends, on any install without the external music packs |
| `src/audio/adl/sound_adlib.cpp` (1 of 22) | `unlock()` before the early return for an unknown opcode | The lock was left held | Harmless where the lock is a no-op (desktop); a hang where it is real |

**2. Real game speed below 60 fps** — `src/gameloop.c` (~30)

| Change | Why | Without it |
|---|---|---|
| One game step per timer tick, catching up on queued ticks, skipping ahead past 8 behind; draw at least every 6 ticks | Game time jumped to the timer, losing ticks between slow frames; the loop only drew when the timer queue was empty | Below 60 fps the game itself slows (46% speed at 27 fps on a 486); a machine too slow for the game speed never draws |

**3. Save options when the options menus close** — `src/gameloop.c` (~9), `src/newui/menu.c` (5)

| Change | Why | Without it |
|---|---|---|
| `GameOptions_Save()` when the in-game Options or Game Controls menu closes, and on leaving Options and Extras | Options were only written on a clean exit | Changes are lost when the game is killed, reset or switched off (common on DOS; a crash or Task Manager on the desktop) |

**4. Compact build list option** — `src/newui/actionpanel.c` (134), `src/opendune.c` (10), `src/enhancement.c/.h` (9), `src/config_a5.c` (1), `src/newui/menu_extras.c` (4)

| Change | Why | Without it |
|---|---|---|
| Optional two-column grid of small icons with READY, percentage and pause sign on the icons; the build widget uses the whole panel when on | Short sidebars show one item at a time (320x240); more items fit at any size | Off by default: nothing changes unless a player turns it on |

**5. Build without ENet** — `src/net/net_none.c` (new, 205), `src/net/net.h` (1), `src/net/net_enet.c` (6), `src/newui/menu_lobby.c` (17), `cmake/FileList.cmake` (part)

| Change | Why | Without it |
|---|---|---|
| `net_none.c` keeps single player's local command path (Dune Dynasty routes single-player commands through the network message buffers); `Net_IsAvailable()`; the lobby hides multiplayer when unavailable | Platforms without a network stack (DOS, minimal builds) | A build without ENet cannot play at all: stubbing the net code drops every single-player command |

**6. The MS-DOS target**

| File | Change | Why | Without it |
|---|---|---|---|
| `CMakeLists.txt` (68), `cmake/FileList.cmake` (~50) | `MSDOS` branch: DJGPP flags, Allegro 4, LTO option, DOS file list, compatibility include path, optional package publishing | Build the DOS target | No DOS build |
| `src/config_a5.c` (27) | `DUNEDYN.CFG` name; two `[audio]` keys; apply and swap command-line overrides | 8.3 file names; DOS mixer settings; overrides for one run (`-res`, `-voices`, ...) | DOS truncates `dunedynasty.cfg` to `DUNEDYNA.CFG`; no DOS sound settings; a one-off `-res` would be saved |
| `src/audio/adl/sound_adlib.cpp` (21), `.h` (1) | Interrupt-safe `lock()`/`unlock()` on DOS; `tick()`, one driver step without generating samples | DOS drives a real OPL chip from a 72 Hz timer interrupt | No AdLib music on DOS (no software synth there), or races between the interrupt and the main program |
| `src/gameloop.c` (~35) | `timer/perf.h` hooks (overlay timings, game tick count, mission summaries); kept shaded background behind hints and options | F10 performance overlay and `DUNEDYN.LOG`; hint and options screens at full speed on a 486 | No overlay or log on DOS; hint and options screens at half speed (29 vs 57 fps on a DX2) |
| `src/timer/perf.h` (new, 38) | Hook macros, no-ops except on DOS | One line per hook in shared code | An `#ifdef __DJGPP__` block at each hook |
| `src/gui/gui.c` (25) | Menubar and sidebar cache hook | Redraw them at 15 Hz on DOS | About 6 ms more per frame on a 486 |
| `src/newui/viewport.c` (32) | Terrain layer hook: redraw only changed cells | Software rendering on a 486 | 2-4 ms more per frame at 320x240, far more at 640x480 |
| `src/os/endian.h` (2), `src/os/strings.h` (4) | Treat DJGPP like MinGW (little-endian; its own `snprintf` and `strdup`) | DJGPP's headers | Build errors |
| `.gitignore` (5) | DOS and Windows cross-build directories | Tidy working tree | |

Profiling timers used while optimising (`PERF_SECTION_*`) are not left
in shared files; `perf.h` keeps the macros for adding them temporarily.

### Order and testing

1. Pull requests 1 to 5 first: useful on their own, and small.
2. Then 6, with a `dos-djgpp-build.yml` workflow matching the existing
   per-platform workflows (caching DJGPP and Allegro 4.2).

Before submitting:

- Build and run Linux and Windows. The Windows build cross-compiles
  here (`scripts/windows/build-windows-x64-cross.sh`) and runs on the
  user's Windows 11 PC: settings kept, saves load both ways with DOS.
- Real-hardware testing (486 to Pentium III).
- Ask upstream whether they want the DOS target, and who maintains it.
- Licences: Allegro 4 (giftware), CWSDPMI (redistributable with its
  docs), DJGPP runtime. Ship stock binaries and sources only.
- Decide whether the 86Box/DOSBox-X test scripts go upstream: they need
  user-supplied MS-DOS 6.22 disks and BIOS ROMs, so perhaps as optional
  developer tooling with docs only.
