# Dune Dynasty for MS-DOS: plan and status

Goal: Dune Dynasty (OpenDUNE's engine and fixes plus Dune Dynasty's
quality-of-life features and skirmish) running on real MS-DOS hardware,
486 to Pentium III, and offered upstream as a build target alongside
the existing Linux, macOS and Windows builds.

Bugs, fixes and features to offer upstream are tracked in
[UPSTREAM.md](UPSTREAM.md). Player documentation is
`scripts/dos/package/README.TXT`; the test rig is described in
`scripts/dos/README-86box.md`.

## Approach

- Cross-compiled on Linux with DJGPP (gcc 12) into a 32-bit protected
  mode program running under CWSDPMI (`cmake/djgpp.cmake`,
  `scripts/dos/setup-toolchain.sh`).
- Platform layer on **stock Allegro 4.2**, the last Allegro with DOS
  support. Allegro is not modified or wrapped; anything it lacks is
  done beside it (e.g. the PIT channel 2 clock in `src/timer/perf_dos.c`).
- Game logic is shared with the desktop builds. DOS code lives in
  `*_dos.*` files and `__DJGPP__` branches, so desktop builds are
  unchanged.
- Save games stay compatible: existing save chunks are not changed;
  new state goes in new optional chunks with a plain fallback.
- **86Box is the reference** for testing (it shows slow interrupt
  handlers and DMA/IRQ timing that DOSBox-X hides). DOSBox-X is only
  for quick smoke tests.

## Phases

| Phase | Status | Left to do |
|---|---|---|
| 1. Headless build | Done | — |
| 2. 8bpp software renderer | Done, plus the speed work below | Rotated/scaled sprites and high-res overlays not supported; `rubblemask.png` is only used by the desktop renderer (effect not yet checked); faster 640x480 on 486s |
| 3. Input and timer | Done | Arrow-key scrolling; mouse wheel and Ctrl/Alt/Shift combinations untested; mouse drivers other than CuteMouse; game-speed fix (decision) |
| 4. Audio | Done: Sound Blaster samples, real OPL2/OPL3, MPU-401 MIDI | Not measured in large battles; MP3 music packs for Pentium-class machines (optional) |
| 5. Config, files, memory | Mostly done: `DUNEDYN.CFG`, run log, settings saved when the options menus close, DOS saves load | **8 MB machines** (needs about 10 MB now); loading desktop saves untested; CPU-tuned builds (`DOS_CPU` exists, only the 486 build is packaged) |
| 6. Skirmish | Works | CPU-scaling defaults (player count, unit caps) not exposed; more skirmish options (see features) |
| 7. Multiplayer (Watt-32 + ENet) | Not started | Planned last |
| 8. Upstream audit | Not started | OpenDUNE commits after the fork; recent gameflorist commits; upstream pull requests |
| Verification on real hardware | Not started | 486 to Pentium III machines; so far 86Box plus one user VM |

## Done beyond the original phases

- Performance overlay (F10, `-overlay`) and `DUNEDYN.LOG` run log with
  a system report and per-mission summaries.
- Rendering speed on 486s (rendering only, game logic unchanged):
  15 Hz menubar/sidebar cache, link-time optimisation, integer blit
  path, terrain layer redrawing only changed cells, no back buffer clear
  between in-game frames, kept background behind hints and options.
  Atreides mission 3, 486DX2/66 VLB in 86Box at 320x240: about 30 ms per
  frame down to 16.7 ms (60 fps, full game speed); hint screen 34.8 ms
  down to 17.6 ms.
- Mouse cursor drawn straight to the screen, independent of frame rate.
- Fixed upstream: jukebox crash when advancing to a group with no
  playable songs.
- Settings saved when the options menus close, not only on a clean exit.
- Test rig: 86Box machine templates, scripted input and screenshots,
  `DD_PROFILE` / `-profcoarse` timing, packaging to a share
  (`DOS_PUBLISH_DIRS`).

## Decisions pending

- **Game-speed fix.** Dune Dynasty runs at most one game step per drawn
  frame, so below 60 fps the game itself slows (about 75% speed at
  45 fps). Fix: run every overdue step before drawing, with a cap.
  Changes pacing on slow machines.
- **Vision updates at a fixed 15 Hz** (deferred). `Unit_UpdateMap` runs
  for every unit every frame (~5 ms on a DX2) and is how stationary
  units notice enemies. Running it at a fixed rate would be cheaper and
  the same on every machine, but shifts spotting latency slightly.

## Known issues and open questions

### Correctness
- **Desktop build not compiled since the DOS work.** Allegro 5 is not
  installed on the build machine, so shared changes (`src/os/cfgfile.c`
  replacing `al_config`, `src/os/fs.c`, `Net_IsAvailable`, the additions
  to `input_a5.c` and `video_a5.c`, `GameOptions_Save` on menu close)
  were only compile-checked as far as possible. Must build and run the
  Linux and Windows targets before any upstream pull request.
- **No frames when game ticks can't keep up.** The game loop only draws
  when the timer queue is empty (`GameLoop_Loop`). If processing ticks
  takes longer than the 60 Hz tick period (seen with the gprof build),
  nothing is drawn at all. Real 486s don't hit this today, but a large
  skirmish on a slow machine might. Related to the game-speed fix.
- Settings that a command-line switch overrides for one run (`-res`,
  `-voices`, `-quality`, `-allaudio`) are saved with their pre-override
  values, so changing them in the options menu during that run is not
  kept.
- 320x200 is not playable: the sidebar has no room for build items.

### Untested
- On real hardware: everything. In 86Box: mouse clicks with a real
  mouse driver work (user's VM); keypad scrolling with NumLock on and
  off, Ctrl/Alt/Shift combinations and the mouse wheel are untested.
- MIDI music only checked by counting notes sent; not listened to on a
  real General MIDI or MT-32 module.
- Sound in large battles: Allegro clips by saturation when many sounds
  mix; Dune Dynasty throttles repeats of one sample within 8 ticks.
  Neither measured on DOS.
- Desktop save games on DOS; Dune Dynasty's extra controls; missions
  after the third.

### Cosmetic
- The overlay's font has no lower case, so it reads in capitals.
- The overlay's game speed can read slightly over 100% (measurement
  window jitter).
- Credits counter, build progress and minimap update at 15 Hz, by
  design of the sidebar cache.

### Test rig
- Scripted input uses fixed coordinates; `play-atreides-1.inp` only
  reaches the build buttons at 640x480.
- The user's 4DPS 86Box VM does not boot in the rig (its CMOS expects
  another disk); a staged VM in `86box-dx2-66-vlb/` is the reference.
- gprof does not work: its sampling interrupt does not survive Allegro
  taking over the timer. Use `DD_PROFILE` sections instead.

### Upstream bugs found (details in UPSTREAM.md)
- `Shape_Width`/`Shape_Height` fallbacks look swapped (`src/shape.c`).
- Unreachable 3440/3840 resolution branches (`src/gfx.c`).
- Per-frame game-state work in the draw path makes unit spotting depend
  on frame rate.

## Backlog

### Usability
- Compact build list at 320x240: two columns of small icons, 4 items
  visible instead of 1 (`src/newui/actionpanel.c`).
- Arrow-key scrolling.
- README: OPL-only music (`dune2_midi=0`); maybe a `-nomidi` switch.
- Missing cursor with mouse drivers other than CuteMouse (needs a
  `DUNEDYN.LOG` from that setup).

### Performance
- Copy to screen (3.3 ms at 320x240): skip comparing areas known to be
  unchanged, such as the cached sidebar.
- Fog edges (~2 ms) and blitter tuning (~2-3 ms).
- 640x480 on 486s: cache more of the viewport's dynamic parts.
- Per-frame unit update (~5 ms on a DX2, more with many units): only
  the deferred 15 Hz vision change would remove it (see Decisions).
- Explain 45 fps on the user's 4DPS/dynarec VM against 60 fps on the
  test VM (compare with dynarec off, or the staged test VM).

### Platform and compatibility
- 8 MB machines: tight atlas packing, free the 1 MB render buffer, lock
  only interrupt-handler memory, trim the C++ runtime.
- Untested on DOS: desktop save games, control groups, build queues,
  rally points, later missions, large battles (frame rate and sound).

### Gameplay features (platform-neutral, save-compatible; see UPSTREAM.md)
- Better pathfinding (breadth-first or A*, same 14-step route format).
- Guard mode that reacts to attacks on nearby units and buildings.
- Attack-move, and queued orders.
- More skirmish options, such as "Surprise me".
- AI that defends its base better without cheating.

### Upstream
- Pull requests: bug fixes and portable helpers first (config and
  directory helpers, building without ENet), then the MS-DOS target
  with a `dos-djgpp-build.yml` workflow.

## Suggested order

1. Game-speed fix (if approved), compact build list, arrow keys.
2. Scripted 86Box test pass for the untested features.
3. 8 MB support.
4. Real-hardware runs on the user's machines.
5. OpenDUNE and gameflorist audit.
6. Gameplay features, starting with pathfinding or guard mode.
7. Multiplayer.

## Target machines

| Machine | Expected |
|---|---|
| 486 DX2/66 | 320x240 (`LOW.BAT`), VLB or PCI video, real OPL or MPU music, 4 sounds |
| 486 DX4/100, Am5x86-133 | 320x240 at full speed; 640x480 usable |
| Pentium 75-133 | 640x480 (`MEDIUM.BAT`), 6-player skirmish |
| Pentium MMX 166-233 | 800x600 (`HQ.BAT`), all audio |
| Pentium II/III | 800x600 and up, large skirmishes; LAN multiplayer later |
