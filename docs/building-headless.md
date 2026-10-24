# Building and testing headless on Linux

Everything here runs on a Linux x86-64 machine with no display and no
root access: the MS-DOS build, the Windows x64 build, and automated test
runs of the MS-DOS build in emulated 486 and Pentium PCs. No Windows or
DOS machine is needed until testing on real hardware.

The commands assume the repository root as the working directory. Tools
go into `~/djgpp` and `~/tools` by default; every script takes
environment variables to put them elsewhere (see the comment at the top
of each script).

Host packages used: `cmake` (3.10 or later; tested with 3.31), `curl`, `git`, `python3`
(with Pillow for screenshots), `zstd`, `mtools`, `dosfstools`,
`syslinux` (for its `mbr.bin`) and, to prime 86Box machines, `Xvfb` and
`xdotool`. Where these cannot be installed system-wide, the scripts also
look for private copies (see "Without root" below).

## MS-DOS

### Toolchain

    scripts/dos/setup-toolchain.sh

Installs the newest build-djgpp release (the DJGPP gcc cross compiler)
into `~/djgpp`, builds Allegro 4.2, the last Allegro with DOS support,
for DJGPP from the head of its 4.2 branch, and fetches the newest
CWSDPMI (the DPMI host the game runs under) from DJGPP's archive into
`~/tools/dl/cwsdpmi`. Allegro is used unmodified. Nothing is pinned:
what is already installed is kept, so delete it to update.
`setup-toolchain.sh versions` prints what it would install.

The 86Box test runs also need **CuteMouse**: `ctm.zip` (CuteMouse 2.1)
unpacked so that `~/tools/dl/ctmouse/bin/CTMOUSE.EXE` exists (`CTMOUSE`).

### Build

    cmake -S . -B build-dos -DCMAKE_TOOLCHAIN_FILE=cmake/djgpp.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-dos -j"$(nproc)"

The program is `build-dos/dist/dunedyn.exe`. Options:

| Option | Default | Meaning |
|---|---|---|
| `DJGPP_PREFIX` | `~/djgpp` | Where `setup-toolchain.sh` installed DJGPP |
| `DOS_CPU` | `i486` | `-march` for the build (`pentium`, `pentium-mmx`, ... for faster machines) |
| `DOS_LTO` | `ON` | Link-time optimisation (a few percent faster on a 486) |
| `DOS_PUBLISH_DIRS` | empty | Semicolon-separated directories to update with the packaged game after every build |

### Package

    scripts/dos/package.sh [destination ...]

Assembles `build-dos/package/DUNEDYN/`: `DUNEDYN.EXE`, `CWSDPMI.EXE`, the
player README, a default `DUNEDYN.CFG`, the `LOW`/`MEDIUM`/`HQ` batch
files, licences and `BUILD.TXT`, the build date and commit (with
`+changes` if the working tree had uncommitted edits). Each destination
gets `DUNEDYN/` updated in place; game data in `DATA/`, an existing
`DUNEDYN.CFG` and logs are left alone. The Dune II 1.07 data files go in
`DATA/` (not included).

## Windows x64, cross-compiled

Upstream builds Windows on Windows with MSYS2 (see the README). The same
build can be cross-compiled on Linux with the same MSYS2 libraries:

1. Unpack an llvm-mingw release (`llvm-mingw-*-ucrt-ubuntu-*-x86_64.tar.xz`
   from <https://github.com/mstorsjo/llvm-mingw/releases>) into
   `~/tools/llvm-mingw` (`LLVM_MINGW`).
2. Fetch the MSYS2 UCRT64 packages the CI installs, and their
   dependencies:

       scripts/windows/fetch-msys2-deps.py ~/tools/mingw-ucrt64

3. Build:

       scripts/windows/build-windows-x64-cross.sh

`build-win64/dist/` then holds `dunedynasty.exe`, every DLL it needs
(found by following its imports) and the game's static files: copy it
to a Windows PC together with the game data. `SRC=<path>` builds another
source tree, such as a worktree of a pull request branch, with the same
setup.

Windows ARM64 works the same way in principle (llvm-mingw also targets
`aarch64-w64-mingw32`, and MSYS2 has a CLANGARM64 repository), but is not
scripted; the GitHub workflow on a fork is the easier check (below).

## Testing the MS-DOS build headless

86Box is the reference: it emulates real 486 and Pentium boards, sound
cards and video cards closely enough to show slow interrupt handlers and
DMA timing problems. DOSBox-X is only a quick check that a build starts.

### 86Box

Setup (MS-DOS 6.22 boot disk, ROMs, CMOS priming) and the emulated
machines are described in [scripts/dos/README-86box.md](../scripts/dos/README-86box.md).
A run:

    scripts/dos/run-86box.sh dx2-66-vlb 120

boots a fresh copy of the MS-DOS disk with the current build, runs the
game and copies `OUT.TXT`, `DUNEDYN.LOG` and screenshots (as PNG) to
`~/tools/86box/vms/dx2-66-vlb/out/`. Useful variables:

| Variable | Meaning |
|---|---|
| `AUTOSHOT=300,900` | Screenshot at these GUI ticks (1/60 s) |
| `AUTOEXIT=` | Exit at this tick (default: last screenshot + 5) |
| `AUTOINPUT=<file>` | Scripted input (format below) |
| `CFG=<file>` | Use this as `DUNEDYN.CFG`, e.g. `scripts/dos/tests/640x480.cfg` |
| `ARGS="-voices 4"` | Command line switches |
| `STAGE_DIR=<dir>` | Directory with `DUNEDYN/` (game data, saves) to copy onto the disk |
| `MEM=8192` | Memory size in KB instead of the machine's |
| `PROFILE=<tick>` | Time frames from this tick (`DD_PROFILE`); add `ARGS=-profcoarse` for totals only |

`DUNEDYN.LOG` has the system report, settings, memory use at each stage
and per-mission frame time summaries.

### Scripted input

`AUTOINPUT` (the game's `DD_AUTOINPUT`) is a file of `<tick>:<action>`
lines; ticks are GUI timer ticks (1/60 s) since start-up, and
coordinates are in the 320x200 menu space, so a script works at any
resolution:

| Action | Meaning |
|---|---|
| `C<x>;<y>` | Left click |
| `R<x>;<y>` | Right click |
| `M<x>;<y>` | Move the mouse |
| `K<hex>[;<n>]` | Press a scancode, release it after n ticks (default 4) |
| `H<hex>;<n>` | Hold a scancode (e.g. Shift) for n ticks while later actions run |
| `D<x1>;<y1>;<x2>;<y2>` | Drag with the left button |
| `S` | Centre on the player's construction yard (or MCV) and click it |

For example, from the main menu to the first saved game, then a
screenshot a few seconds later:

    450:C115;144
    600:C167;82

with `AUTOSHOT=1200`. DOS limits environment lines to 127 characters, so
longer scripts go in a file (the runner copies it onto the disk).

### DOSBox-X

    scripts/dos/smoke-dosbox-x.sh [seconds]

Runs the build in DOSBox-X (`DOSBOX_X_ROOT`, an unpacked DOSBox-X
package) with the same `AUTOSHOT`/`AUTOEXIT`/`AUTOINPUT` variables. Good
for "does it start"; not for timing, sound or video problems.

## CI on a fork

The `.github/workflows/*-build.yml` workflows build Linux, macOS,
Windows and MS-DOS on GitHub's runners on every push. On a fork they are
off until the fork's owner enables them once in the fork's Actions tab.
Each run uploads the packaged build as an artifact.

`dos-build.yml` runs `setup-toolchain.sh` (cached by the versions it
resolves), builds, packages and runs `scripts/dos/ci-smoke.sh`: the game
is started in DOSBox without game data and has to load under CWSDPMI and
report the missing data files, like the Linux and macOS workflows' check
for "data directory". CI cannot run the 86Box tests, which need an
MS-DOS boot disk, BIOS ROMs and the game data.

## Without root

On a machine where packages cannot be installed, the scripts look for
private copies first and fall back to the system's:

- `~/tools/hostpkgs/root`: Debian packages unpacked with `dpkg-deb -x`
  (mtools, syslinux, xdotool and the libraries they need); override with
  `MTOOLS_BIN`, `SYSLINUX_MBR`, `HOSTPKGS`.
- `~/tools/xvfb`: an Xvfb run from that directory (`XVFB_DIR`). A
  packaged Xvfb looks for `xkbcomp` at an absolute path; a private copy
  can be binary-patched to look in a relative directory of the same
  length (for example `./xkbbin`) holding `xkbcomp`.
- `setup-toolchain.sh` bundles the flex runtime that DJGPP's prebuilt
  `ar` and `ranlib` need, if it is missing.
