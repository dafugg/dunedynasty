#!/bin/sh
# Assemble the MS-DOS distribution directory: build-dos/package/DUNEDYN.
#
# Usage: scripts/dos/package.sh [destination-to-copy-to]
# Env:   CWSDPMI_DIR  directory with CWSDPMI.EXE and cwsdpmi.doc

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
CWSDPMI_DIR="${CWSDPMI_DIR:-$HOME/tools/dl/cwsdpmi/bin}"
OUT="$ROOT/build-dos/package/DUNEDYN"

rm -rf "${OUT:?}"
mkdir -p "$OUT/DATA"

cp "$ROOT/build-dos/dist/dunedyn.exe" "$OUT/DUNEDYN.EXE"
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

ls -la "$OUT"

if [ $# -ge 1 ]; then
	rm -rf "${1:?}/DUNEDYN"
	cp -r "$OUT" "$1/"
	echo "Copied to $1/DUNEDYN"
fi
