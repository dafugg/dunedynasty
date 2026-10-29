# The best Dune Dynasty installation

What to install, and what to leave out, for the best Dune Dynasty
experience: which Dune II data, which fixes and mods, and why. Applies
to every platform, including the MS-DOS build. Items marked
**(untested)** are worked out from the files and the code but not yet
tried in the game.

Dune II data is not included with Dune Dynasty, and nothing here
changes that: every file below comes from your own copy of Dune II or
from the fan patches and mods named, which you download yourself.

## 1. Dune II data: v1.07, EU or US

Dune Dynasty needs the `*.PAK` files of Dune II **v1.07**. v1.0 does not
work.

| Release | `DUNE2.EXE` size | Notes |
|---|---|---|
| EU v1.07, "The Battle for Arrakis" | 371616 bytes | English, French and German. The data Dune Dynasty is written for. |
| US v1.07, "The Building of a Dynasty" | 367760 bytes | English only. Common abandonware and eXoDOS copies. Unit voices need Dune Dynasty with [#81](https://github.com/gameflorist/dunedynasty/pull/81) (in the MS-DOS build already). |
| HitSquad v1.07 (EU budget re-release) | 371474 bytes | Like EU. |
| v1.0 | 365504 bytes | Not supported. Update to v1.07 first. |

The size of `DUNE2.EXE` is the quickest way to tell them apart; Dune
Dynasty itself only uses the PAK files. The EU checksums are in
`data/FILELIST.TXT`.

Differences that matter with the US data (from a full comparison of
both releases): six English voice files are named without their `Z`
prefix, which silenced unit acknowledgements until #81; there is no
`FRENCH.PAK` or `GERMAN.PAK`, so keep the language English; the fonts
are about 1 pixel wider per character; and a few texts differ
(credits, two Harkonnen mentat topics). Scenarios are identical.

Copy the PAK files into Dune Dynasty's `data` directory (`DATA` on
MS-DOS).

## 2. MrFlibble's Dune II v1.07 Unofficial Fix: use two of its files

`dune2_107_fix.rar` (2011) patches the original game. It has one folder
per release; pick the one for your data:

| Your data | Folder |
|---|---|
| US v1.07 | `DYNASTY` |
| EU v1.07 | `ARRAKIS` |
| HitSquad | `HITSQUAD` |

For Dune Dynasty, copy only these two files from that folder into
`data`:

- **`ENGLISH.PAK`**: fixes the spelling of "Ornithopter" (shown as
  "Ornithipter"), and corrects the mentat database: the power use of the
  Barracks and Starport (10 and 50, not 20 and 80), the Light Factory
  entry from mission 2 for Atreides and Ordos, and no Spice Silo entry in
  mission 1. Dune Dynasty fixes none of these itself.
- **`VOC.PAK`**: removes the clicking noise from seven sound effects
  (gunfire, small explosions, the missile launch, the drop-off and the
  worm). For US data, the `DYNASTY` `VOC.PAK` keeps the US voice names,
  so it works with #81. See also section 4 on sound.

Leave out:

- **`SCENARIO.PAK`**: Dune Dynasty already fixes almost everything in it
  by itself for the original campaign (the AI "team bug", `MaxUnits`,
  Atreides WOR instead of Barracks, the SCENA005 spice bloom, Devastator
  and Ornithopter reinforcements, looped Sardaukar reinforcements,
  duplicate buildings). Worse, the fix writes the Starport Ornithopter
  entries as `'Thopter`, which its own patched `DUNE2.EXE` understands
  but Dune Dynasty does not, so with it the Starport would probably stop
  offering Ornithopters in missions 20 to 22 **(untested)**. What it adds
  beyond that is small: higher AI unit limits in SCENO021 and the
  starting credits in SCENO022.
- **`DUNE2.EXE`, `SETUP.EXE` and the rest**: these only change the
  original program, which Dune Dynasty replaces.

The eXoDOS installation of Dune II already has the `ARRAKIS` files
installed over the EU release, including `SCENARIO.PAK`; take
`SCENARIO.PAK` from an unpatched copy instead, or use the original one.

## 3. Building animations: install `rep-hi-hv-fix.zip`

A replacement `ICON.ICN` and `ICON.MAP` (2012) that fix three
buildings' animations. EU and US v1.07 have the same icon map for these,
so it suits both:

- **Hi-Tech Factory:** in the original, the dome never opens; the fix
  adds the frames where it opens.
- **Repair Facility:** the door jumps from closed to open; the fix adds
  the frames in between.
- **Heavy Vehicle Factory:** one frame showed the vehicle already out;
  the fix brings it out in steps.

Copy `icon.icn` and `icon.map` into `data` (as `ICON.ICN` and `ICON.MAP`
on MS-DOS). Dune Dynasty reads loose files in `data` before the PAK
files, takes the icon map from the data, and has room for the ten extra
icons the fix adds. The frames were checked by rendering them from both
the original and the fixed files.

The windtrap animation, which an early Dune Dynasty version got wrong
with other icon maps, has been fixed since 2012 and needs nothing.

## 4. Sound effects: pops and clicks

Many Dune II sound effects have a DC offset, which pops and clicks when
several play together. Two fixes exist, and both replace `VOC.PAK`:

- the fix's `VOC.PAK` above (seven effects), and
- Tiddalick's **normalized VOC.PAK**, which Dune Dynasty's README
  recommends:
  <https://www.moddb.com/games/dune-ii-the-building-of-a-dynasty/addons/vocpak-normalized>

Which is better, and whether the normalized one exists for the US voice
names, is not checked yet. Until then, use the fix's `VOC.PAK` for your
release.

## 5. Custom campaigns

Dune Dynasty plays four fan campaigns. Put each one's files in its
directory under `campaign` (`CAMPAIGN` on MS-DOS), next to the
`META.INI` already there; then use the arrows beside the subtitle in the
main menu:

| Directory (MS-DOS) | Campaign | Files |
|---|---|---|
| `dune2x` (`DUNE2X`) | Dune 2 eXtended v1.29, <http://forum.dune2k.com/topic/18360-dune-2-extended-project/> | `CONQUEST.PAK`, `ENGLISH2.PAK`, `EXPAND.PAK` from `DUNEX129.ZIP` |
| `mfas` (`MFAS`) | MrFlibble's Alternate Scenarios | `SCENARIO.PAK` |
| `shac` (`SHAC`) | Stefan Hendriks' Atreides Campaign, <http://arrakis.dune2k.com/downloads.html> | `REGIONA.INI`, `SCENA001.INI` to `SCENA022.INI` |
| `superdune2` (`SUPERDUN`) | Super Dune II Classic v1.5g, <http://forum.dune2k.com/topic/20065-super-dune-ii-classic/> | the `*.SD2` files (unpack `INSTALL.EXE` with UnRAR) |

All four have been played to mission 1 on the MS-DOS build. A home-made
campaign's `META.INI` must not start with the `[CAMPAIGN]` line itself:
put a comment line first, or the campaign is not found (a Dune Dynasty
bug, not yet fixed).

## 6. Music

Dune Dynasty plays the original AdLib and MIDI music, and external music
packs (see the README for the list and where they go). On MS-DOS, music
goes to a real OPL chip and to an MPU-401 MIDI device; see the MS-DOS
`README.TXT`.

## 7. Settings

Gameplay enhancements are in **Options and Extras**; all are off unless
noted, so the game plays like Dune II until you change them. Suggestions
for a modern feel: health bars, fog of war, the compact build list on
small screens (in game, Options > Game controls, last page), and, with
#83, placing concrete under units and deploying MCVs on concrete.

## Status of this guide

| Item | State |
|---|---|
| EU and US data | Compared file by file; US voices fixed by #81 |
| `ENGLISH.PAK`, `VOC.PAK` from the fix | Recommended from the file comparison; not yet played |
| Leaving out the fix's `SCENARIO.PAK` | From the code; the Ornithopter effect is untested |
| Icon fix | Recommended; frames checked by rendering the files |
| Normalized VOC.PAK vs the fix's | To compare |
| Custom campaigns | Tested on MS-DOS |
