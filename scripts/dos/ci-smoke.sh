#!/bin/sh
# Smoke test for CI: start the packaged DUNEDYN.EXE in DOSBox without game
# data and check that it starts under CWSDPMI and reports the missing
# data files, as the Linux and macOS workflows do with "data directory".
#
# Usage: scripts/dos/ci-smoke.sh [package dir]   (build-dos/package/DUNEDYN)
# Env:   DOSBOX   DOSBox executable (dosbox)

set -eu

PKG="${1:-build-dos/package/DUNEDYN}"
DOSBOX="${DOSBOX:-dosbox}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/c"
cp -R "$PKG" "$work/c/DUNEDYN"
rm -f "$work/c/DUNEDYN/DATA/"*.PAK "$work/c/DUNEDYN/DUNEDYN.LOG"

cat > "$work/dosbox.conf" <<EOF
[sdl]
output=surface
[cpu]
cycles=max
[mixer]
nosound=true
[autoexec]
mount c "$work/c"
c:
cd \\DUNEDYN
DUNEDYN.EXE > OUT.TXT
exit
EOF

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	timeout 120 "$DOSBOX" -conf "$work/dosbox.conf" -noconsole >/dev/null 2>&1 || true

log="$work/c/DUNEDYN/DUNEDYN.LOG"
[ -f "$log" ] || { echo "DUNEDYN.LOG was not written: the program did not start" >&2; exit 1; }
tr -d '\r' < "$log"

grep -q "DPMI: .*CWSDPMI" "$log" || { echo "No DPMI line: did CWSDPMI load?" >&2; exit 1; }
grep -q "Dune II data files not found" "$log" || { echo "Missing-data message not found" >&2; exit 1; }
echo "Smoke test passed"
