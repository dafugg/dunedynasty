#!/bin/sh
# Quick headless smoke test of DUNEDYN.EXE in DOSBox-X.
#
# DOSBox-X is forgiving (ISR timing, DMA, etc.); this only checks that
# the build starts.  86Box is the reference for anything timing related.
#
# Usage: scripts/dos/smoke-dosbox-x.sh [seconds]
# Env:   DOSBOX_X_ROOT  unpacked dosbox-x package root (~/tools/dosbox-x/root)
#        STAGE_DIR      directory containing DUNEDYN/ (~/tools/dos-stage)
#        EXE            exe to test (build-dos/dist/dunedyn.exe)
#        AUTOSHOT       GUI tick(s) (1/60 s) to save SHOTnnnn.PCX at, comma separated (default 300)
#        AUTOEXIT       tick to exit at (default last AUTOSHOT + 5)
#        AUTOINPUT      scripted input, see InputDOS_PollAutomation (DOS limits lines to 127 chars)

set -eu

SECONDS_TO_RUN="${1:-30}"
DOSBOX_X_ROOT="${DOSBOX_X_ROOT:-$HOME/tools/dosbox-x/root}"
STAGE_DIR="${STAGE_DIR:-$HOME/tools/dos-stage}"
EXE="${EXE:-build-dos/dist/dunedyn.exe}"
AUTOSHOT="${AUTOSHOT:-300}"
AUTOEXIT="${AUTOEXIT:-$((${AUTOSHOT##*,} + 5))}"
AUTOINPUT="${AUTOINPUT:-}"
CAPTURE_AUDIO="${CAPTURE_AUDIO:-}"     # non-empty: record audio to $STAGE_DIR/CAPTURE/*.wav
CAPTURE_MIDI="${CAPTURE_MIDI:-}"       # non-empty: record MIDI output to $STAGE_DIR/CAPTURE/*.mid
CFG="${CFG:-}"
# A script file is copied next to the exe and passed as @AUTO.INP.
AUTOINPUT_FILE=""
if [ -n "$AUTOINPUT" ] && [ -f "$AUTOINPUT" ]; then
	AUTOINPUT_FILE="$AUTOINPUT"
	AUTOINPUT="@AUTO.INP"
fi

cp "$EXE" "$STAGE_DIR/DUNEDYN/DUNEDYN.EXE"
rm -f "$STAGE_DIR/DUNEDYN/AUTO.INP"
[ -n "$AUTOINPUT_FILE" ] && cp "$AUTOINPUT_FILE" "$STAGE_DIR/DUNEDYN/AUTO.INP"
rm -f "$STAGE_DIR/DUNEDYN/OUT.TXT" "$STAGE_DIR/DUNEDYN/DUNEDYN.LOG" "$STAGE_DIR/DUNEDYN/DUNEDYN.OLD" "$STAGE_DIR"/DUNEDYN/SHOT*.PCX

RUN=DUNEDYN.EXE
if [ -n "$CAPTURE_AUDIO$CAPTURE_MIDI" ]; then
	rm -rf "${STAGE_DIR:?}/CAPTURE"
	mkdir -p "$STAGE_DIR/CAPTURE"
	RUN="DX-CAPTURE ${CAPTURE_AUDIO:+/A }${CAPTURE_MIDI:+/M }DUNEDYN.EXE"
fi

ORIG_CFG=""
if [ -n "$CFG" ]; then
	ORIG_CFG=$(mktemp)
	cp "$STAGE_DIR/DUNEDYN/DUNEDYN.CFG" "$ORIG_CFG" 2>/dev/null || true
	cp "$CFG" "$STAGE_DIR/DUNEDYN/DUNEDYN.CFG"
fi

conf=$(mktemp --suffix=.conf)
trap 'rm -f "$conf"; if [ -n "$ORIG_CFG" ]; then cp "$ORIG_CFG" "$STAGE_DIR/DUNEDYN/DUNEDYN.CFG"; rm -f "$ORIG_CFG"; fi' EXIT

cat > "$conf" <<CONF
[sdl]
output=surface
[dosbox]
machine=svga_s3
memsize=16
[cpu]
core=normal
cputype=pentium
cycles=max
[sblaster]
sbtype=sb16
[dosbox]
captures=$STAGE_DIR/CAPTURE
[autoexec]
mount c "$STAGE_DIR"
c:
cd \\DUNEDYN
set DD_AUTOSHOT=$AUTOSHOT
set DD_AUTOEXIT=$AUTOEXIT
set DD_AUTOINPUT=$AUTOINPUT
set DD_MIDILOG=${MIDILOG:-}
$RUN > OUT.TXT
exit
CONF

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
LD_LIBRARY_PATH="$DOSBOX_X_ROOT/usr/lib/x86_64-linux-gnu" \
timeout -k 3 "$SECONDS_TO_RUN" "$DOSBOX_X_ROOT/usr/bin/dosbox-x" \
	-nopromptfolder -fastlaunch -defaultconf -conf "$conf" -exit >/dev/null 2>&1 || status=$?

echo "dosbox-x exit status: ${status:-0} (124 = still running at timeout)"
for pcx in "$STAGE_DIR"/DUNEDYN/SHOT*.PCX; do
	[ -f "$pcx" ] || continue
	png="${pcx%.PCX}.png"
	python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])' "$pcx" "$png"
	echo "screenshot: $png"
done

for f in OUT.TXT DUNEDYN.LOG; do
	if [ -f "$STAGE_DIR/DUNEDYN/$f" ]; then
		echo "---- $f"
		cat "$STAGE_DIR/DUNEDYN/$f"
	fi
done
if [ -n "$CAPTURE_AUDIO$CAPTURE_MIDI" ]; then
	ls -la "$STAGE_DIR/CAPTURE" 2>/dev/null
fi
