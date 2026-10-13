DUNE DYNASTY FOR MS-DOS  (experimental build)
=============================================

Dune Dynasty is an enhanced Dune II engine based on OpenDUNE. This is a
port of it to MS-DOS, for real 486 and Pentium machines.

You need the original Dune II 1.07 data files: they are not included.


SETUP
-----
1. Copy this directory to your hard disk, e.g. C:\DUNEDYN.
2. Copy the Dune II 1.07 *.PAK files (DUNE.PAK, ENGLISH.PAK, ATRE.PAK,
   HARK.PAK, ORDOS.PAK, SCENARIO.PAK, SOUND.PAK, VOC.PAK, INTRO.PAK,
   INTROVOC.PAK, MENTAT.PAK, FINALE.PAK, MERC.PAK, XTRE.PAK, HERC.PAK)
   into C:\DUNEDYN\DATA.
3. Load a mouse driver (MOUSE.COM, CTMOUSE, ...).
4. Make sure BLASTER is set for digital sound, e.g.
     SET BLASTER=A220 I5 D1 H5 T6
5. Run DUNEDYN.EXE. CWSDPMI.EXE must be in the same directory or on the
   PATH (it is the DPMI host; it is not needed under Windows 9x).


REQUIREMENTS
------------
- 486 or better with an FPU (486DX, DX2, DX4, Am5x86, Pentium, ...).
- 16 MB RAM. 8 MB is not enough yet.
- VLB or PCI SVGA card with VESA support for 640x480. A VESA 2.0 BIOS or
  UniVBE / SciTech Display Doctor gives a linear frame buffer, which is
  faster. Any VGA card can use 320x200.
- Sound Blaster compatible card for digital sound (optional).
- AdLib / OPL2 / OPL3 for music and synth effects (optional).
- MPU-401 (UART mode) for MIDI music on a General MIDI module or an
  MT-32 (optional). The port is taken from BLASTER's P field, or 330h.


MIDI MUSIC
----------
The game picks songs at random from every enabled music set, so to hear
only MIDI, turn the AdLib set off in DUNEDYN.CFG:

  [music]
  dune2_adlib=0
  dune2_midi=1
  default=dune2_midi

  [audio]
  midi_format=gm          (gm for General MIDI / SC-55, mt32 for MT-32)

Synth sound effects still use the OPL chip when there is one.


PICKING A RESOLUTION
--------------------
Edit DUNEDYN.CFG ([graphics] section):

  320x240      Recommended for 486s. Dune II's look, works on any VGA card
               (Mode-X). About 45 fps on a DX2/66 VLB, 60 fps on a
               DX4/100 PCI or Am5x86-133.
  640x480      Default. Shows four times as much of the map as Dune II.
               Needs VESA. About 15 fps on a DX2/66 VLB, 27 on a DX4/100
               PCI, 55-60 on a Pentium MMX 233.
  800x600      Pentium MMX and up: about 58 fps on a PMMX 233.
  640x480 with menubar_scale=2, sidebar_scale=2, viewport_scale=2
               Looks like 320x240, doubled; slower than 640x480 at 1x.
  320x200      Not playable yet: the sidebar has no room for build items.

At 320x240 the build list shows one item at a time. Turn on the compact
build list (Options and Extras > Gameplay > "Compact two-column build
list", or compact_build_list=1 in the [enhancement] section): two columns
of small icons, as many rows as fit (4 items at 320x240, about 20 at
640x480). Ready items say READY, items being built show a percentage,
items on hold a pause sign.

Frame rates were measured in the 86Box emulator; real hardware will
differ. Press F10 for the performance overlay (see PERFORMANCE OVERLAY
AND LOG), F12 for a screenshot (SHOTnnnn.PCX).


QUICK START: PICK A SPEED
-------------------------
  LOW.BAT      486 DX2/DX4/5x86   320x240, 4 sounds at once
  MEDIUM.BAT   5x86-133, Pentium  640x480, 8 sounds at once
  HQ.BAT       Pentium MMX and up 800x600, 12 sounds, all audio on

Measured in 86Box, skirmish: LOW about 45 fps on a DX2/66 VLB and 60 on
a DX4/100 PCI; MEDIUM about 40 fps on an Am5x86-133 or Pentium 133; HQ
about 58 fps on a Pentium MMX 233 (31 on a Pentium 133).

The batch files only change settings for that run; DUNEDYN.CFG keeps
yours. Extra switches are passed on, e.g. LOW -nosound.


COMMAND LINE
------------
These apply to one run only and are not saved to DUNEDYN.CFG.

  -res WxH        Screen size: 320x240, 640x480, 800x600, 1024x768, ...
  -scale N        Menubar, sidebar and map scale (1 or 2). 2 looks like
                  Dune II but is slower than 1 at the same resolution.
  -voices N       Mix at most N digital sounds at once (1 to 12).
  -quality N      Mixer quality: 0 fastest, 2 best, 3 automatic.
  -allaudio       Turn on music, synth and digital effects and speech.
  -nosound        Do not touch any sound hardware (troubleshooting).
  -overlay        Start with the full performance overlay shown.


KEYS
----
Map and view
  Arrow keys         Scroll the map (also keypad 1-9 for diagonals)
  F4                 Jump to your base
  Tab                Cycle through your buildings
  Space              Centre on the selected building
  - and =            Zoom the map out and in (also keypad - and +, or
                     the mouse wheel with a wheel mouse driver)
  [ and ]            Change the menubar / sidebar scale

Selecting units
  Drag with left     Select units in a box
  Shift + click      Add to the selection
  Ctrl + click       Select all units of that type on screen (or
                     double-click)
  Ctrl + 1-0         Make the selection control group 1-0
  1-0                Select control group 1-0
  Alt + 1-0          Select control group 1-0 and centre on it
  Right button drag  Pan the map

Commands
  A, M, R, G         Attack, Move, Retreat, Guard (first letter of
                     any button's label)
  Build list keys    Shown in the corner of each item's icon (e.g. X
                     for Concrete); Shift + key to reduce or cancel
  D                  Starport: send the order
  S                  Palace: fire the superweapon
  Esc                Close dialogs, cancel placing or targeting

Music
  F5                 Show the current track
  F6 and F7          Music volume down and up (Shift: this track only)

MS-DOS build only
  F10                Performance overlay: off, frames per second, full
  F12                Screenshot, saved as SHOTnnnn.PCX

Not yet tried on real hardware: the Ctrl, Alt and Shift combinations
and the mouse wheel.


SOUND SETTINGS
--------------
There is no sound emulation: music goes to the OPL chip and the MPU-401,
digital sounds to the Sound Blaster. The one software cost is mixing
several digital sounds into the Sound Blaster's single stream, which
runs in its interrupt. In DUNEDYN.CFG, [audio] section:

  voices=12          Sounds mixed at once, 1 to 12. Dune II played one at
                     a time; Dune Dynasty plays up to 12.
  mixer_quality=3    0 fastest, 1, 2 best (interpolated), 3 automatic:
                     0 on a 486, 2 on a Pentium or better.
  enable_sounds=both none, synth (OPL effects), digital, or both.
  enable_music=1     0 for no music.
  enable_voices=1    0 for no spoken messages.

Measured on a DX2/66 in 86Box at 320x240, campaign mission 1: all sound
costs about 5% (48.6 fps with -nosound, 46 fps with everything on).
Voices and mixer quality made no measurable difference in that scene;
large battles with many sounds at once have not been measured yet.


SLOWER AND SMALLER MACHINES
---------------------------
- Use 320x240 on a 486 (see above). 640x480 redraws four times as many
  pixels every frame and is bound by memory speed on a 486.
- A VLB or PCI video card matters as much as the CPU at 640x480.
- For VESA modes, a VESA 2.0 BIOS or UniVBE gives a linear frame
  buffer, which is faster than banked access.
- If sound is choppy or the game stutters in big battles, try
  voices=4 or voices=1, and mixer_quality=0.
- 16 MB RAM is needed for now, at any resolution: the game uses about
  10 MB. 8 MB support is planned.


WHAT WORKS
----------
Tested in the 86Box emulator (486 and Pentium machines, MS-DOS 6.22):
- Intro, main menu, house selection, mentats, campaign mission 1:
  selecting units and buildings, move orders, starting a build.
- Skirmish: lobby (left click a house's second box for you, right click
  for an AI opponent), random maps, starting a game.
- Digital sound effects and voices (Sound Blaster), AdLib music and
  synth effects on the real OPL chip, MIDI music on an MPU-401
  (checked by counting the notes sent; not listened to on a real
  sound module yet).

Not yet tested on DOS: Dune Dynasty's extra controls (drag selection,
control groups, build queues, rally points, right button panning),
placing buildings, saving and loading, later missions.

PERFORMANCE OVERLAY AND LOG
---------------------------
F10 cycles the overlay: off, frames per second, full. -overlay starts
with it full. The numbers are refreshed twice a second:

  58 fps  16.9 ms  worst 25  speed 100%
      Frames per second, average and worst frame time in milliseconds,
      and game speed: game steps run against the steps due. "paused"
      while a hint, the mentat or the options menu stops the game.
  game 0.3  gui 0.4  draw 11.1
      Milliseconds per frame in game logic and AI, interface logic,
      and drawing the map, units and sidebar.
  clear 1.0  present 1.8  changed 0%
      Milliseconds per frame clearing the drawing buffer and copying it
      to the video card, and how much of the screen changed.
  music 0.9%  sfx 0/12  free 5.1 MB
      CPU time in the AdLib and MIDI music interrupts, digital sounds
      playing out of the voices available, and free memory.
  Intel 486DX4 ~100 MHz  320x240
      CPU and clock (~ means estimated: 486s have no cycle counter),
      and screen size.

The game keeps real time below 60 fps: game steps that fall due between
frames are all run, so a slow machine shows fewer frames, not a slower
game. "speed" stays near 100% unless the machine cannot keep up even
with drawing skipped.

Each run writes DUNEDYN.LOG in the game directory: what was detected
(CPU, DOS, memory, VESA, mouse, sound and music hardware), the settings
used, warnings and errors, and a performance summary after each mission
and at exit. The previous logs are kept in DUNEDYN.OLD (started afresh
once it passes 256 KB). Please include both when reporting a problem.


NOT YET
-------
- Recorded music packs (MP3, FLAC, ...).
- Multiplayer (no network support on DOS yet).
- 8 MB machines.

Errors and warnings are written to DUNEDYN.LOG in the game directory.


LICENCES
--------
Dune Dynasty and OpenDUNE are GPL v2: see LICENSE.TXT.
CWSDPMI is (C) Charles W Sandmann: see CWSDPMI.DOC.
