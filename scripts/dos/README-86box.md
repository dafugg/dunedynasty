# MS-DOS test rig

86Box is the reference for timing, ISRs and video/audio hardware behaviour.
DOSBox-X (`smoke-dosbox-x.sh`) only checks that a build starts.

Building the game, the scripted input format and the runner's options are
in [docs/building-headless.md](../../docs/building-headless.md).

## One-time setup

1. `scripts/dos/setup-toolchain.sh`: installs DJGPP and Allegro 4.2.
2. Get 86Box v6.0 (AppImage) and its ROM set into `~/tools/86box/` (`86Box.AppImage`, `roms/`; `BOX` to use another place).
3. Stage the game in `~/tools/dos-stage/DUNEDYN/`:
   - `DATA/`: the Dune II 1.07 `*.PAK` files
   - `CWSDPMI.EXE`: from csdpmi7b.zip

   Other directories can be staged too and chosen per run with `STAGE_DIR`, e.g. one with saved games in `DUNEDYN/SAVE/`.
4. Build the MS-DOS 6.22 boot disk from your own MS-DOS 6.22 Disk 1 image (not included):

       scripts/dos/make-dos-hdd.sh <Disk1.img> ~/tools/86box/vms/dos622-base.img

   This partitions and formats a 64 MB disk (130/16/63) and installs the MS-DOS boot sector, `IO.SYS`, `MSDOS.SYS` and `COMMAND.COM`, which is what `SYS C:` would do. No interactive install is needed.
5. Prime each machine's CMOS once. Otherwise POST stops at "CMOS checksum error – press F1":

       scripts/dos/prime-86box-nvr.sh <machine>

   This boots the machine on a virtual X display (Xvfb) and presses the keys to save the BIOS setup with xdotool. It works for Award BIOS boards. Machines that use the same board can share the result: copy `~/tools/86box/primed-nvr/<machine>/`.

## Running

    scripts/dos/run-86box.sh <machine> [timeout-seconds]

Each run uses a fresh copy of the base disk with the current `build-dos/dist/dunedyn.exe`, CuteMouse and a test `AUTOEXEC.BAT`. It boots headless and stops once `AUTOEXEC.BAT` finishes. It then copies `OUT.TXT`, `DUNEDYN.LOG` and screenshots (converted to PNG) to `~/tools/86box/vms/<machine>/out/`.

The runner's options (screenshots, scripted input, configs, memory size, profiling) are listed at the top of `run-86box.sh` and in [docs/building-headless.md](../../docs/building-headless.md). Screenshots are `SHOTnnnn.PCX`, as F12 saves interactively.

## Machines (`scripts/dos/86box/*.cfg`)

| Config | CPU | Board | Video |
|---|---|---|---|
| dx2-66-vlb | i486DX2/66 | ASUS PVI-486SP3 (SiS 496) | Cirrus GD5428 VLB |
| dx4-100-vlb | iDX4/100 | ASUS PVI-486SP3 | Cirrus GD5428 VLB |
| dx4-100-pci | iDX4/100 | ASUS PVI-486SP3 | S3 Trio64 PCI |
| am5x86-133-vlb | Am5x86-133 | ASUS PVI-486SP3 | Cirrus GD5428 VLB |
| p133-pci | Pentium 133 | ASUS P/I-P55T2P4 (i430HX) | S3 Trio64 PCI |
| pmmx233-pci | Pentium MMX 233 | ASUS P/I-P55T2P4 | S3 Trio64 PCI |

All machines have an SB16 and a PS/2 mouse; the 486s have 16 MB and the Pentiums 32 MB (`MEM=` overrides it). CPUs use the interpreter, not the dynarec.
