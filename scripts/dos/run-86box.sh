#!/bin/sh
# Boot a test machine in 86Box (headless) and run DUNEDYN.EXE once.
#
# Usage: scripts/dos/run-86box.sh <machine> [seconds]
#   <machine> is a template in scripts/dos/86box/<machine>.cfg
#
# The machine boots MS-DOS 6.22 from a copy of BASE_IMG, runs the game
# with DD_AUTOSHOT/DD_AUTOEXIT set, and the results (OUT.TXT, DUNEDYN.LOG,
# screenshots) are copied out of the disk image into OUT_DIR.
#
# Env: BASE_IMG  MS-DOS base image from make-dos-hdd.sh (~/tools/86box/vms/dos622-base.img)
#      STAGE_DIR directory containing DUNEDYN/ with DATA/ (~/tools/dos-stage)
#      EXE       exe to test (build-dos/dist/dunedyn.exe)
#      AUTOINPUT scripted input, see InputDOS_PollAutomation (DOS limits lines to 127 chars)
#      RUN       what to run (DUNEDYN.EXE; "COMMAND /C HQ.BAT" for a batch file)
#      ARGS      command line arguments, e.g. "-voices 4"
#      CFG       game config file to use as DUNEDYN.CFG (e.g. scripts/dos/tests/320x200.cfg)
#      PRESENT   "full" to send the whole screen every frame (default: changed spans only)
#      PROFILE   GUI tick to start the frame time profile at (0)
#      MEM       memory size in KB instead of the template's (e.g. 8192)
#      AUTOSHOT  GUI tick(s) (1/60 s) to screenshot, comma separated (300); AUTOEXIT tick to exit (last AUTOSHOT + 5)

set -eu

MACHINE="$1"
SECONDS_TO_RUN="${2:-90}"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TEMPLATE="$SCRIPT_DIR/86box/$MACHINE.cfg"
BOX="${BOX:-$HOME/tools/86box}"
BASE_IMG="${BASE_IMG:-$BOX/vms/dos622-base.img}"
STAGE_DIR="${STAGE_DIR:-$HOME/tools/dos-stage}"
EXE="${EXE:-build-dos/dist/dunedyn.exe}"
CTMOUSE="${CTMOUSE:-$HOME/tools/dl/ctmouse/bin/CTMOUSE.EXE}"
MTOOLS_BIN="${MTOOLS_BIN:-$HOME/tools/hostpkgs/root/usr/bin}"
AUTOSHOT="${AUTOSHOT:-300}"
AUTOEXIT="${AUTOEXIT:-$((${AUTOSHOT##*,} + 5))}"
AUTOINPUT="${AUTOINPUT:-}"
PROFILE="${PROFILE:-0}"
PRESENT="${PRESENT:-}"
CFG="${CFG:-}"
# A script file is copied next to the exe and passed as @AUTO.INP.
AUTOINPUT_FILE=""
if [ -n "$AUTOINPUT" ] && [ -f "$AUTOINPUT" ]; then
	AUTOINPUT_FILE="$AUTOINPUT"
	AUTOINPUT="@AUTO.INP"
fi

VM_DIR="$BOX/vms/$MACHINE"
OUT_DIR="${OUT_DIR:-$VM_DIR/out}"
IMG="$VM_DIR/hdd.img"
PART="$IMG@@32256"     # Partition at LBA 63.

export MTOOLS_SKIP_CHECK=1
mcopy() { "$MTOOLS_BIN/mcopy" "$@"; }

[ -f "$TEMPLATE" ] || { echo "No machine template $TEMPLATE"; exit 1; }

rm -rf "${VM_DIR:?}"
mkdir -p "$VM_DIR/nvr" "$OUT_DIR"
cp "$BASE_IMG" "$IMG"

# A fresh CMOS stops POST at "CMOS checksum error - press F1".  Reuse a
# CMOS saved once from BIOS setup (see scripts/dos/README-86box.md).
PRIMED_NVR="$BOX/primed-nvr/$MACHINE"
if [ -d "$PRIMED_NVR" ]; then
	cp "$PRIMED_NVR"/* "$VM_DIR/nvr/"
else
	echo "warning: no primed CMOS in $PRIMED_NVR; POST will likely stop at a CMOS error"
fi

# ---- Disk contents --------------------------------------------------------
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

printf 'FILES=30\r\nBUFFERS=20\r\n' > "$tmp/CONFIG.SYS"
# Progress markers in C:\ show how far the boot got if the run hangs.
printf '@ECHO OFF\r\nSET BLASTER=A220 I5 D1 H5 T6\r\nECHO booted > C:\\STEP1.TXT\r\nCTMOUSE > C:\\MOUSE.TXT\r\nECHO mouse > C:\\STEP2.TXT\r\nCD \\DUNEDYN\r\nSET DD_AUTOSHOT=%s\r\nSET DD_AUTOEXIT=%s\r\nSET DD_AUTOINPUT=%s\r\nSET DD_PROFILE=%s\r\nSET DD_PRESENT=%s\r\nSET DD_MOUSELOG=%s\r\n%s %s > OUT.TXT\r\nECHO done > DONE.TXT\r\n' \
	"$AUTOSHOT" "$AUTOEXIT" "$AUTOINPUT" "$PROFILE" "$PRESENT" "${MOUSELOG:-}" "${RUN:-DUNEDYN.EXE}" "${ARGS:-}" > "$tmp/AUTOEXEC.BAT"

mcopy -i "$PART" "$tmp/CONFIG.SYS" "$tmp/AUTOEXEC.BAT" "$CTMOUSE" ::/
mcopy -i "$PART" -s "$STAGE_DIR/DUNEDYN" ::/
mcopy -o -i "$PART" "$EXE" ::/DUNEDYN/DUNEDYN.EXE
[ -n "$AUTOINPUT_FILE" ] && mcopy -o -i "$PART" "$AUTOINPUT_FILE" ::/DUNEDYN/AUTO.INP
[ -n "$CFG" ] && mcopy -o -i "$PART" "$CFG" ::/DUNEDYN/DUNEDYN.CFG
"$MTOOLS_BIN/mdel" -i "$PART" ::/DUNEDYN/OUT.TXT ::/DUNEDYN/DUNEDYN.LOG ::/DUNEDYN/DUNEDYN.OLD 2>/dev/null || true
for f in $("$MTOOLS_BIN/mdir" -b -i "$PART" ::/DUNEDYN 2>/dev/null | grep -i 'SHOT.*PCX' || true); do
	"$MTOOLS_BIN/mdel" -i "$PART" "$f"
done

sed -e "s|@HDD@|$IMG|" ${MEM:+-e "s|^mem_size = .*|mem_size = $MEM|"} "$TEMPLATE" > "$VM_DIR/86box.cfg"

# ---- Run --------------------------------------------------------------------
echo "Booting $MACHINE for up to ${SECONDS_TO_RUN}s..."
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
	"$BOX/86Box.AppImage" -N -R "$BOX/roms" -L "$VM_DIR/86box.log" -P "$VM_DIR" \
	>"$VM_DIR/stdout.log" 2>&1 &
box_pid=$!

# 86Box does not exit by itself: stop it once AUTOEXEC.BAT has finished
# (DONE.TXT appears; the guest is then idle at the DOS prompt).
elapsed=0
while [ $elapsed -lt "$SECONDS_TO_RUN" ] && kill -0 $box_pid 2>/dev/null; do
	sleep 5
	elapsed=$((elapsed + 5))
	if "$MTOOLS_BIN/mtype" -i "$PART" ::/DUNEDYN/DONE.TXT >/dev/null 2>&1; then
		sleep 2
		break
	fi
done
kill -9 $box_pid 2>/dev/null || true
wait $box_pid 2>/dev/null || true
echo "Stopped 86Box after ~${elapsed}s"

# ---- Results ----------------------------------------------------------------
rm -f "$OUT_DIR"/*
for f in OUT.TXT DUNEDYN.LOG DONE.TXT GMON.OUT; do
	mcopy -n -i "$PART" "::/DUNEDYN/$f" "$OUT_DIR/$f" 2>/dev/null || true
done
for f in STEP1.TXT MOUSE.TXT STEP2.TXT; do
	mcopy -n -i "$PART" "::/$f" "$OUT_DIR/$f" 2>/dev/null || true
done
for f in $("$MTOOLS_BIN/mdir" -b -i "$PART" ::/DUNEDYN 2>/dev/null | grep -i 'SHOT.*PCX' || true); do
	pcx="$OUT_DIR/$(basename "$f")"
	mcopy -n -i "$PART" "$f" "$pcx"
	python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])' "$pcx" "${pcx%.*}.png"
	echo "screenshot: ${pcx%.*}.png"
done

[ -f "$OUT_DIR/DONE.TXT" ] && echo "AUTOEXEC.BAT completed" || echo "AUTOEXEC.BAT did NOT complete"
for f in STEP1.TXT MOUSE.TXT STEP2.TXT OUT.TXT DUNEDYN.LOG; do
	[ -f "$OUT_DIR/$f" ] && { echo "---- $f"; cat "$OUT_DIR/$f"; }
done
true
