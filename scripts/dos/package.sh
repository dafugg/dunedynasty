#!/bin/sh
# Assemble the MS-DOS distribution directory: build-dos/package/DUNEDYN.
#
# Usage: scripts/dos/package.sh [destination ...]
#   Each destination gets DUNEDYN/ updated in place: program, docs and
#   batch files are replaced; DATA/ (game files), an existing DUNEDYN.CFG
#   and logs are left alone. CAMPAIGN/ gets the campaign definitions
#   (META.INI etc.) updated; the campaigns' own files are left alone.
# Env:   CWSDPMI_DIR  directory with CWSDPMI.EXE and cwsdpmi.doc
#        EXE          program to package (build-dos/dist/dunedyn.exe)

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
CWSDPMI_DIR="${CWSDPMI_DIR:-$HOME/tools/dl/cwsdpmi/bin}"
OUT="$ROOT/build-dos/package/DUNEDYN"

rm -rf "${OUT:?}"
mkdir -p "$OUT/DATA"

cp "${EXE:-$ROOT/build-dos/dist/dunedyn.exe}" "$OUT/DUNEDYN.EXE"
cp "$CWSDPMI_DIR/CWSDPMI.EXE" "$OUT/CWSDPMI.EXE"

# Text files with DOS line endings.
crlf() { sed 's/\r$//; s/$/\r/' "$1" > "$2"; }
crlf "$SCRIPT_DIR/package/README.TXT" "$OUT/README.TXT"
crlf "$SCRIPT_DIR/package/DUNEDYN.CFG" "$OUT/DUNEDYN.CFG"
crlf "$ROOT/LICENSE.txt" "$OUT/LICENSE.TXT"
crlf "$CWSDPMI_DIR/cwsdpmi.doc" "$OUT/CWSDPMI.DOC"
for bat in LOW MEDIUM HQ; do
	crlf "$SCRIPT_DIR/package/$bat.BAT" "$OUT/$bat.BAT"
done
printf 'Copy the Dune II 1.07 *.PAK files here.\r\n' > "$OUT/DATA/README.TXT"

# Custom campaign definitions; the campaigns themselves are not included.
# superdune2 is too long for an MS-DOS directory name.
for dir in "$ROOT"/static/general/campaign/*/; do
	name=$(basename "$dir")
	[ "$name" = superdune2 ] && name=superdun
	name=$(echo "$name" | tr a-z A-Z)
	mkdir -p "$OUT/CAMPAIGN/$name"
	for f in "$dir"*.INI; do
		crlf "$f" "$OUT/CAMPAIGN/$name/$(basename "$f")"
	done
done

# Which build this is, to tell copies apart.
rev=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)
git -C "$ROOT" diff --quiet HEAD -- 2>/dev/null || rev="$rev+changes"
printf 'Built %s from %s\r\n' "$(date '+%Y-%m-%d %H:%M')" "$rev" > "$OUT/BUILD.TXT"

for dest in "$@"; do
	mkdir -p "$dest/DUNEDYN/DATA"
	for f in "$OUT"/*; do
		name=$(basename "$f")
		[ "$name" = DATA ] && continue
		[ "$name" = CAMPAIGN ] && { cp -R "$f" "$dest/DUNEDYN/"; continue; }
		[ "$name" = DUNEDYN.CFG ] && [ -f "$dest/DUNEDYN/DUNEDYN.CFG" ] && continue
		cp "$f" "$dest/DUNEDYN/$name"
	done
	[ -n "$(ls -A "$dest/DUNEDYN/DATA")" ] || cp "$OUT/DATA/README.TXT" "$dest/DUNEDYN/DATA/"
	echo "Updated $dest/DUNEDYN ($(cat "$OUT/BUILD.TXT" | tr -d '\r'))"
done
