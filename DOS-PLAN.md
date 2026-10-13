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
| 3. Input and timer | Done, including real-time game speed below 60 fps | Mouse wheel and Ctrl/Alt/Shift combinations untested; mouse drivers other than CuteMouse |
| 4. Audio | Done: Sound Blaster samples, real OPL2/OPL3, MPU-401 MIDI | Not measured in large battles; MP3 music packs for Pentium-class machines (optional) |
| 5. Config, files, memory | Mostly done: `DUNEDYN.CFG`, run log, settings saved when the options menus close, DOS saves load | 16 MB minimum; 8 MB postponed (needs 10.4-11.5 MB, see Memory use); loading desktop saves untested; CPU-tuned builds (`DOS_CPU` exists, only the 486 build is packaged) |
| 6. Skirmish | Works | CPU-scaling defaults (player count, unit caps) not exposed; more skirmish options (see features) |
| 7. Multiplayer (Watt-32 + ENet) | Not started | Planned last |
| 8. Upstream audit | Done 2026-10-13 (UPSTREAM.md): nothing to port from OpenDUNE | gameflorist `feat-mt32-init` would need DOS hooks if merged |
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
- Compact build list option (off by default, any resolution): two
  columns of small icons, as many rows as fit; READY / percentage /
  pause sign on the icons. Screenshots for a pull request are on the
  share (`compact-build-list/`).
- Game keeps real time below 60 fps: one game step per timer tick,
  catching up on queued ticks (skipping ahead past 8 behind), and a frame
  is drawn at least every 6 ticks. Atreides mission 3 at 640x480 on a
  DX2/66 (86Box): 27 fps; game speed 46% before, 95% after.
- Test rig: 86Box machine templates, scripted input and screenshots,
  `DD_PROFILE` / `-profcoarse` timing, packaging to a share
  (`DOS_PUBLISH_DIRS`).

## Memory use

Dune II ran in about 2 MB. This port currently needs about 10.4 MB at
320x240 (campaign) and 11.5 MB at 640x480 (skirmish), measured in
86Box on 2026-10-13. On an 8 MB machine, CWSDPMI gets about 7 MB: the
game used all of it before its graphics had finished loading, and
exited without an error in the log.

### Where it goes (320x240 campaign, Atreides mission 3)

| What | MB | Notes |
|---|---|---|
| Program, before `main` | 3.6 | code 1.6 (Allegro 0.54, game 0.63, libc and C++ runtime 0.25, more with link-time optimisation); static data 1.25 (the fog-of-war table `g_mapVisible` alone is 0.42); stack 0.5 (DJGPP default) |
| Sprite atlases | 2.8 | four 1024-wide 8bpp bitmaps: interface 860 rows, terrain tiles 596, units 559, map regions 600 |
| Sound samples | 1.5 | 114 samples (one house's voices plus effects), loaded at mission start |
| Graphics work buffer | 1.0 | `GFX_Init`'s 1024x1024 buffer, only needed at that size while building the atlases |
| Screen buffers and caches | 0.5 | back and front buffers, terrain layer, interface cache, minimap |
| Wasted while loading | 0.4-0.9 | each atlas starts at 1024x1024 and is trimmed into a new copy; the holes left behind are only partly reused |

The peak is what counts:
- All memory is locked at start-up (`_CRT0_FLAG_LOCK_MEMORY`,
  `src/common_dos.c`) so that the timer, music and sound interrupts
  never touch memory that has been swapped out. This also stops CWSDPMI
  from using its swap file, so everything must fit in RAM.
- DJGPP's heap never gives memory back, so the most ever allocated is
  what has to fit.

### Why so much more than Dune II

- **Pre-drawn sprites instead of drawing from compressed data.** Dune II
  kept its graphics compressed and decoded and recoloured them as it
  drew. Dune Dynasty was written for GPUs: at start-up it decodes every
  sprite into texture atlases, with a separate copy in each house's
  colours (six) for units and buildings, plus copies of fonts in each
  of their colours. The DOS port keeps the same design, with 8bpp
  atlases instead of 32bpp textures.
- **All of a mission's samples are loaded up front.** Voices and sound
  effects are kept in memory for the whole mission (1.5 MB).
- **32-bit code and a general library.** The game is written in
  portable C, compiled for 32-bit protected mode, with Dune Dynasty's
  extra features (skirmish, the improved AI, network code). Allegro
  brings all of its video, sound and MIDI drivers.
- **Tables sized for the general case.** Static arrays are sized for the
  largest map and every house: the fog-of-war table keeps a cause and a
  64-bit timeout per house for each of 4096 tiles (0.42 MB), and there
  are network message buffers and backup unit and structure pools for
  multiplayer.
- **Higher resolutions.** Back buffers and caches grow with the screen:
  about 1.1 MB more at 640x480 than at 320x240.
- **DPMI overhead.** A 0.5 MB stack, page tables, and locked memory with
  no swapping.

### Measuring

- `DUNEDYN.LOG` has a `Memory ...` line at each stage of start-up and at
  each mission's start and end: physical memory in use, the change since
  the last line, the most so far (sampled every 64 frames as well) and
  the heap's use.
- `MEM=8192 scripts/dos/run-86box.sh <machine>` runs a test machine with
  8 MB.
- Large allocations by caller: build with `-DDD_MEMTRACE` and link with
  `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc` (each allocation of
  4 KB or more goes to the log with its return address; match it
  against `nm -n` of the executable).

### Decision: 16 MB minimum, memory reduction postponed (2026-10-13)

Machines of this era usually have 2, 4, 8 or 16 MB. DOS keeps the
first megabyte, so an 8 MB machine leaves CWSDPMI about 7 MB, less any
drivers and disk cache. Supporting 8 MB properly would mean getting
well below 7 MB, not just under it, and even then 2 and 4 MB machines
would stay out of reach. The plan below would at best reach about
7.3-7.8 MB plus swapping, so 16 MB stays the minimum and this work is
postponed. The plan is kept for reference.

### Plan to fit 8 MB (estimated savings, postponed)

1. Build each atlas at its final size, without the temporary
   1024x1024 bitmaps: 0.4-0.9 MB.
2. Shrink the graphics work buffer to the 320 KB the game needs once
   the atlases are built: about 0.7 MB.
3. Link only the Allegro drivers used, with Allegro's public driver-list
   macros (Allegro itself unchanged): 0.1-0.3 MB.
4. Recolour house-coloured units and buildings while drawing, as Dune
   II did, instead of keeping six copies: probably over 1 MB, for a
   table lookup per pixel on those sprites.
5. Let CWSDPMI swap out large blocks that no interrupt reads (atlases,
   map-screen graphics), keeping everything else locked. Machines with
   enough memory behave as now.

Steps 1-3 should give about 8.5-9 MB and step 4 about 7.3-7.8 MB, still
more than the about 7 MB an 8 MB machine has free, so step 5 is needed
as well. Other options, which change code shared with the desktop
builds: a smaller fog-of-war table (about 0.3 MB) and loading voices
when first played (up to 1.5 MB).

## Decisions pending

- **Vision updates at a fixed 15 Hz** (deferred). `Unit_UpdateMap` runs
  for every unit every frame (~5 ms on a DX2) and is how stationary
  units notice enemies. Running it at a fixed rate would be cheaper and
  the same on every machine, but shifts spotting latency slightly.

## Known issues and open questions

### Correctness
- **Desktop build: compiles, not yet run.** The Windows x64 build now
  cross-compiles on Linux (`scripts/windows/build-windows-x64-cross.sh`:
  llvm-mingw plus MSYS2 UCRT64 libraries, the same ones upstream's CI
  uses), with no new warnings from our changes. It still has to be run
  on Windows (user's Windows 11 PC), and the Linux desktop build has
  not been built. Both before any upstream pull request.
  On Windows so far (user): runs to the menu; settings are kept across
  restarts; save games work both ways: a DOS save loads and plays on
  Windows, and a Windows save loads and plays on DOS (86Box, 2026-10-13).
  Since then the desktop build uses upstream's Allegro 5 config, file and
  menu code again (DOS gets them from `src/dos/a5compat/`, see
  UPSTREAM.md), so a quick Windows re-check is due.
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
- Missions after the third.
- Tested in 86Box (Atreides mission 3 saves): drag selection, control
  groups (Ctrl+1, 1), group move orders, build queues and Shift to
  reduce, save and load on DOS, arrow-key scrolling, hint and options
  overlays, game speed below 60 fps.
- Rally points: work (tested by the user, 2026-10-13).

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
- 8 MB machines: postponed; see Memory use.
- Untested on DOS: later missions,
  large battles (frame rate and sound).

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

1. Test pass (scripted 86Box runs), then the Windows cross-compile.
2. Scripted 86Box test pass for the untested features.
3. Real-hardware runs on the user's machines.
4. Pull requests (UPSTREAM.md).
5. Gameplay features, starting with pathfinding or guard mode.
6. Multiplayer.

8 MB support is postponed (see Memory use).

## Target machines

| Machine | Expected |
|---|---|
| 486 DX2/66 | 320x240 (`LOW.BAT`), VLB or PCI video, real OPL or MPU music, 4 sounds |
| 486 DX4/100, Am5x86-133 | 320x240 at full speed; 640x480 usable |
| Pentium 75-133 | 640x480 (`MEDIUM.BAT`), 6-player skirmish |
| Pentium MMX 166-233 | 800x600 (`HQ.BAT`), all audio |
| Pentium II/III | 800x600 and up, large skirmishes; LAN multiplayer later |
