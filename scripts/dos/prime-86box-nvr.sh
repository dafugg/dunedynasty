#!/bin/sh
# Save a valid CMOS for an 86Box machine with an Award BIOS, so that
# automated runs do not stop at "CMOS checksum error - press F1".
#
# Boots the machine on a virtual X display, presses DEL to enter setup,
# then F10, Y, Enter to save, and keeps the resulting nvr/ directory in
# $BOX/primed-nvr/<machine>/ for scripts/dos/run-86box.sh.
#
# Usage: scripts/dos/prime-86box-nvr.sh <machine>
# Env:   XVFB_DIR (~/tools/xvfb, patched Xvfb), HOSTPKGS (~/tools/hostpkgs)

set -eu

MACHINE="$1"
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BOX="${BOX:-$HOME/tools/86box}"
XVFB_DIR="${XVFB_DIR:-$HOME/tools/xvfb}"
HOSTPKGS="${HOSTPKGS:-$HOME/tools/hostpkgs}"
DISPLAY_NUM="${DISPLAY_NUM:-:98}"

VM_DIR="$BOX/vms/prime-$MACHINE"
HOSTLIB="$HOSTPKGS/root/usr/lib/x86_64-linux-gnu"
XDO="$HOSTPKGS/root/usr/bin/xdotool"

rm -rf "${VM_DIR:?}"
mkdir -p "$VM_DIR"
cp "$BOX/vms/dos622-base.img" "$VM_DIR/hdd.img"
sed "s|@HDD@|$VM_DIR/hdd.img|" "$SCRIPT_DIR/86box/$MACHINE.cfg" > "$VM_DIR/86box.cfg"

(cd "$XVFB_DIR" && LD_LIBRARY_PATH="$HOSTLIB" ./Xvfb "$DISPLAY_NUM" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1) &
xvfb_pid=$!
sleep 2

DISPLAY="$DISPLAY_NUM" "$BOX/86Box.AppImage" -N -R "$BOX/roms" -P "$VM_DIR" >"$VM_DIR/stdout.log" 2>&1 &
box_pid=$!
trap 'kill -9 $box_pid $xvfb_pid 2>/dev/null || true' EXIT

xdo() { DISPLAY="$DISPLAY_NUM" LD_LIBRARY_PATH="$HOSTLIB" "$XDO" "$@"; }

sleep 6
xdo mousemove 300 200 click 1
# Keep tapping DEL through POST until setup is reached.
for i in 1 2 3 4 5 6; do
	xdo key Delete
	sleep 1
done
sleep 2
xdo key --delay 300 F10
xdo type --delay 300 Y
xdo key --delay 300 Return
# 86Box writes the NVR on the reset that follows.
sleep 8

mkdir -p "$BOX/primed-nvr/$MACHINE"
cp "$VM_DIR"/nvr/* "$BOX/primed-nvr/$MACHINE/"
ls -la "$BOX/primed-nvr/$MACHINE/"
