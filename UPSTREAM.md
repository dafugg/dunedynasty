# Upstream candidates (gameflorist/dunedynasty)

Bugs and features from the MS-DOS port work that could go back to
https://github.com/gameflorist/dunedynasty as issues or pull requests,
including the MS-DOS build target itself.

Keep this up to date as things are found or fixed.

## Bugs found and fixed here

| Bug | Where | Fix | PR status |
|---|---|---|---|
| **Jukebox crash when a track ends.** Auto-advance moves to the next music group. *Idle (Others)* (after *Idle 9*) lists only external music packs; when none are installed, the fallback looks for a Dune II AdLib track that the group does not have, and passes a NULL track to `Audio_PlayMusicFile` (page fault). Found on DOS (no external packs); affects any install without them. | `src/audio/audio.c` `Audio_PlayMusicGroup`, `Audio_PlayMusicNextInSequence` | Return if no track is found; skip groups with no playable songs when advancing. | Branch `pr/fix-jukebox-crash` |
| `Audio_ScanMusic` resets `count` and `count_default` but not `count_found`, so a second scan would double-count. Latent: only scanned once today. | `src/audio/audio.c` `Audio_ScanMusic` | Reset `count_found` too. | Branch `pr/fix-jukebox-crash` |
| `AdLibDriver::callback` returns early on an unknown opcode without calling `unlock()`. Harmless where lock/unlock are no-ops (desktop); deadlocks or leaves interrupts off where they are real. | `src/audio/adl/sound_adlib.cpp` | `unlock()` before the early return. | Branch `pr/fix-adlib-unlock` |

| **F10 FPS counter invisible in game on the desktop build.** `VideoA5_Tick` drew it at (2, 40) in whatever transform was used last; in game that is the sidebar or menubar, so it only showed in menus and the mentat. Reported on Windows. | `src/video/video_a5.c` `VideoA5_Tick` | Draw in `SCREENDIV_MAIN`, as the software cursor just below does. Only on the PR branch (the DOS build has its own overlay). | Branch `pr/fix-fps-counter-in-game` |
| **Team labels cut off in the lobby.** `ScrollListArea_Draw` clipped "T1".."T6" to `teamWidth` (10 px), narrower than the centred text, so 2, 3, 5 and 6 lost their right edge. Reported on DOS; the code is shared. | `src/newui/scrollbar.c` | Clip to the inside of the label's box. | Branch `pr/fix-team-label-clipping` |
| **Game slows down with the frame rate.** `GameLoop_ProcessGameTimer` jumped game time to the timer, so ticks between slow frames were lost (46% speed at 27 fps on a 486). Also, the loop drew only when the timer queue was empty, so a machine too slow for the game speed never drew. | `src/gameloop.c` | One step per tick, catching up on queued ticks, skipping ahead past 8 behind; draw at least every 6 ticks. Above 60 fps nothing changes. | Branch `pr/game-speed` |

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

## Audit of OpenDUNE and gameflorist (2026-10-13)

**gameflorist/dunedynasty.** `master` has not moved since our base
(4469449c). Unmerged branches:

- `feat-mt32-init` (January 2026): sends the original MT-32
  initialisation at start-up when MT-32 is the MIDI format, and merges
  OpenDUNE's `mt32mpu.c` changes. If it is merged, our DOS files need
  the new hooks: `midi_send_string()` in `src/audio/midi_dos.c` (system
  exclusive messages to the MPU-401), `AudioA5_InitMT32()` in
  `audio_dos.cpp` and `VideoA5_DrawBlankScreen()` in `video_dos.c`.
  Worth having on DOS, where real MT-32s are most likely.
- `feat-ios`, `feat-macos-datadir`, `wip` (January 2025, a large
  graphics-driver experiment): packaging and platform work, nothing for
  us.

Open issues that touch our work: #49 attack-move and #50 queued
orders (our feature list), #57 units not reacting to hostile units
(our guard-response idea), #61 carryalls getting stuck, #48 MT-32
playback (see `feat-mt32-init`), #59 README typo, #66 licence question
(GPL-2.0 only or later; matters for anything we copy in).

**OpenDUNE.** Dune Dynasty forked from OpenDUNE at fc8075a5
(September 2013); OpenDUNE has 703 commits since, to October 2026.
Almost all are for OpenDUNE's own platforms (SDL, Atari, OS/2, Haiku,
Visual Studio), its tools, its INI file or code tidying. Those touching
game logic or shared data handling:

| OpenDUNE commit | What | In Dune Dynasty |
|---|---|---|
| b6c7987f | Spice bloom: tile refresh on the wrong tile | Already fixed (`Unit_Move` has no such refresh) |
| 88d5666d (#171) | Enemy base location ignores slabs and walls | Already fixed (`Structure_SharesPoolElement` check in `Map_Server_FindLocationTile`) |
| 4a0854ee | Radar flag saved twice | Already fixed (`saveload.c`) |
| 83931df7 (#279) | Scenario structures over 100% hitpoints (SCENH006.INI) | Already fixed (clamped with `enhancement_fix_scenario_typos`) |
| 357652c9 | Wrong table in `ReadProfileIni` | Code not in Dune Dynasty |
| b99a6102, 2d0bb1d5 | Sprite zoom width added instead of multiplied, 16-bit overflow | Bug still there (`GUI_DrawSprite_`, `src/gui/gui.c:850`), but no caller uses the zoom flag: dead code. Could be fixed in the bug-fix pull request; no visible effect |
| 5be63825 | `String_Decompress` buffer overflow | Not needed: Dune Dynasty allocates twice the compressed length; the mentat help buffer is 28,000 bytes |
| 0b8d0505 (#175) | Option: Starport respects the unit cap | Dune Dynasty buys Starport units past the cap, as Dune II does (`Server_Recv_PurchaseItemStarport`). Could be an `enhancement_*` option; gameplay change, so not proposed |
| 36387f5f, 2d44f2c4 | Shift+Attack = Ambush, Shift+Guard = Area Guard | Dune Dynasty has its own command keys; not needed |
| 5a912ffe, 548198aa, 35cc0de7, 635b1305, c8082861 | Dune II v1.0 data files (WSA, SHP, CPS, strings) | Not supported by Dune Dynasty; a feature, not a fix |
| b85c04ff | Start voices before MIDI (SDL) | Not relevant (Allegro 5, and DOS) |

Conclusion: nothing from OpenDUNE needs porting. Keep an eye on
`feat-mt32-init`.

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

**1. Bug fixes** (independent of DOS; one pull request each, 1a-1c)

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
| `src/mods/skirmish.c` (6), `src/video/video_poll.h` (new, 15) | `VIDEO_POLL_CURSOR()` between the map generator's stages; a no-op except on DOS | The DOS cursor is drawn by the main program; each generator attempt takes up to 370 ms on a 486, and with many houses dozens fail in a row | The cursor moves about three times a second for seconds at a time while the lobby generates a map |
| `src/timer/perf.h` (new, 38) | Hook macros, no-ops except on DOS | One line per hook in shared code | An `#ifdef __DJGPP__` block at each hook |
| `src/gui/gui.c` (25) | Menubar and sidebar cache hook | Redraw them at 15 Hz on DOS | About 6 ms more per frame on a 486 |
| `src/newui/viewport.c` (32) | Terrain layer hook: redraw only changed cells | Software rendering on a 486 | 2-4 ms more per frame at 320x240, far more at 640x480 |
| `src/os/endian.h` (2), `src/os/strings.h` (4) | Treat DJGPP like MinGW (little-endian; its own `snprintf` and `strdup`) | DJGPP's headers | Build errors |
| `.gitignore` (5) | DOS and Windows cross-build directories | Tidy working tree | |

Profiling timers used while optimising (`PERF_SECTION_*`) are not left
in shared files; `perf.h` keeps the macros for adding them temporarily.

### Pull request branches

One bug or one feature per pull request. Each branch is based on
upstream `master` (4469449c), holds only its own change, and builds for Windows x64 (cross-compiled; no new warnings).
Work on them in the worktree `../dune2qol-pr`. Windows test builds,
each with a `BUILD.TXT` saying what to test, are on the share in
`windows/pr-tests/` (1a-1c share one build). Nothing is pushed yet.

| PR | Branch | Commits | Tested |
|---|---|---|---|
| 1a | `pr/fix-jukebox-crash` | jukebox crash without external music packs | DOS; Windows: to do |
| 1b | `pr/fix-adlib-unlock` | AdLib driver lock left held on an unknown opcode | DOS; nothing visible on Windows |
| 1c | `pr/fix-fps-counter-in-game` | existing F10 FPS counter not visible in game | Windows: to do |
| 1d | `pr/fix-team-label-clipping` | team labels cut off in the lobby | DOS (86Box screenshot); Windows: to do |
| 2 | `pr/game-speed` | real game speed below 60 fps | DOS (86Box); Windows: to do |
| 3 | `pr/save-options` | save options when the menus close | DOS; Windows: to do |
| 4 | `pr/compact-build-list` | compact two-column build list, README and sample config | DOS; Windows: to do |
| 5 | `pr/build-without-enet` | build with `WITH_ENET=OFF` | DOS; Windows with ENet off and on: to do |
| 6 | not yet | the MS-DOS target | Needs 5, and upstream's view first |

Upstream's own build with `WITH_ENET=OFF` fails to link (undefined
`enet_*` symbols), checked with the Windows cross-build.

Upstream uses conventional commit messages (`fix:`, `feat:`) and
adds `CHANGES.txt` entries itself at release time, so the branches
leave `CHANGES.txt` alone; each description below suggests a line.

#### Drafts of the pull request descriptions

**1a. fix: jukebox crash when a track ends without external music packs**

> When a track ends, the jukebox advances to the next music group.
> After Idle 9 comes Idle (Others), which only lists external music
> packs. Without them, the fallback looks for a Dune II AdLib track the
> group does not have and passes NULL to `Audio_PlayMusicFile`. Now
> advancing skips groups with nothing to play, and a missing track
> returns early. The skip reads `count_found`, so `Audio_ScanMusic` now
> resets it as it does the other counts (a second scan would otherwise
> double it).
>
> Suggested CHANGES.txt: "Fix crash when the jukebox advances to a music
> group with no installed songs."

**1b. fix: release the AdLib driver lock on an unknown opcode**

> `AdLibDriver::callback` returned on an unknown opcode without calling
> `unlock()`. No effect while `lock()`/`unlock()` are no-ops, as on the
> desktop; a hang on any platform where they are real.

**1c. fix: F10 FPS counter not visible in game**

> The existing FPS counter (F10) is drawn in whichever transform was
> last used: in game, the sidebar or menubar, so it is off to the side
> or clipped and only shows in menus. It now uses `SCREENDIV_MAIN`, as
> the software cursor just below does.
>
> Suggested CHANGES.txt: "Show the FPS counter (F10) in game."

**1d. fix: team labels cut off in the skirmish and multiplayer lobby**

> The team label (T1 to T6) was clipped to `teamWidth`, 10 pixels, but
> the centred text is wider, so 2, 3, 5 and 6 lost their right edge.
> It is now clipped to the inside of the box drawn around it.

**2. fix: keep real game speed when the frame rate drops below 60 fps**

> `GameLoop_ProcessGameTimer` set game time straight to the timer, so
> game ticks between slow frames were lost and the game slowed down
> with the frame rate. Also, the main loop only drew when no ticks were
> queued, so a machine too slow for the game speed never drew.
>
> Now each game tick runs one step, queued ticks are caught up (up to 8
> behind; after a pause or loading the game skips ahead), and a frame is
> drawn at least every 6 ticks. At 60 fps and above nothing changes.
>
> Measured on a 486 DX2/66 (86Box) at 640x480: game speed went from 46%
> to 95% of real time at 27 fps.
>
> Suggested CHANGES.txt: "Game speed no longer drops when the frame rate
> falls below 60 fps."

**3. feat: save options when the options menus close**

> Options were only written to `dunedynasty.cfg` on a clean exit, so a
> crash, killing the game, or a power-off lost them. They are now also
> saved when the in-game Options or Game Controls menu closes, and when
> leaving Extras.
>
> Suggested CHANGES.txt: "Options are saved when their menu closes, not
> only when quitting."

**4. feat: add compact two-column build list option**

> An optional build list (off by default) with two columns of small
> icons, as many rows as fit, as in Command & Conquer. Costs, shortcuts,
> queue counts, READY, progress and pause are drawn on the icons, and
> rows cut off at the bottom are hidden. At 320x240 it shows four items
> instead of one; at 640x480 all seven of a mission 3 construction
> yard. Works with any resolution and sidebar scale.
>
> Option: Extras > Gameplay > "Compact two-column build list"
> (`compact_build_list` in `[enhancement]`). Documented in the README
> and the sample config. Screenshots attached (share:
> `compact-build-list/`).
>
> Suggested CHANGES.txt: "Add option for a compact two-column build
> list."

**5. feat: build without ENet (WITH_ENET=OFF)**

> `WITH_ENET` exists but `net_enet.c` is always compiled, so turning it
> off fails to link. With it off, `src/net/net_none.c` is built instead.
> A plain stub is not enough: single player passes the local player's
> commands through the client/server message buffers, so `net_none.c`
> keeps those paths and only drops the transport. `Net_IsAvailable()`
> lets the lobby hide hosting and joining ("Not available in this
> build").
>
> For platforms without a network stack, and minimal builds. With
> `WITH_ENET=ON` (the default) nothing changes.

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
