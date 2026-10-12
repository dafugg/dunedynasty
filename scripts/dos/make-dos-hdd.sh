#!/bin/sh
# Build a bootable MS-DOS 6.22 hard disk image for 86Box, without the
# interactive installer: partition, FAT16 format, MS-DOS boot sector,
# then IO.SYS / MSDOS.SYS / COMMAND.COM copied as SYS C: would.
#
# Usage: scripts/dos/make-dos-hdd.sh <MS-DOS 6.22 Disk1.img> <output.img>
#
# Geometry: 130 cylinders x 16 heads x 63 sectors (64 MB).  The 86Box
# config must use the same CHS (hdd_01_parameters = 63, 16, 130, 0, ide)
# because the MS-DOS boot sector reads with INT 13h CHS calls.
#
# Needs mtools (mcopy, mattrib) and mkfs.fat.  MTOOLS_BIN / SYSLINUX_MBR
# override the locations of the locally unpacked packages.

set -eu

DISK1="$1"
OUT="$2"

MTOOLS_BIN="${MTOOLS_BIN:-$HOME/tools/hostpkgs/root/usr/bin}"
SYSLINUX_MBR="${SYSLINUX_MBR:-$HOME/tools/hostpkgs/root/usr/lib/syslinux/mbr/mbr.bin}"
MKFS_FAT="${MKFS_FAT:-/usr/sbin/mkfs.fat}"

CYLS=130
HEADS=16
SECTORS=63
PART_START=$SECTORS                                 # Cylinder 0, head 1.
TOTAL=$((CYLS * HEADS * SECTORS))
PART_SIZE=$((TOTAL - PART_START))

export MTOOLS_SKIP_CHECK=1

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# ---- FAT16 partition with the MS-DOS 6.22 boot code ----------------------
"$MKFS_FAT" -F 16 -C "$tmp/part.img" $((PART_SIZE / 2)) \
	-h $PART_START -g $HEADS/$SECTORS -D 0x80 -M 0xF8 \
	-i 1C2A0D0F >/dev/null

python3 - "$DISK1" "$tmp/part.img" <<'EOF'
import sys
disk1, part = sys.argv[1], sys.argv[2]
with open(disk1, 'rb') as f:
    floppy = f.read(512)
with open(part, 'r+b') as f:
    boot = bytearray(f.read(512))
    # Keep the hard disk BPB (0x0B-0x3D), take MS-DOS's jump, OEM name and code.
    boot[0x00:0x0B] = floppy[0x00:0x0B]
    boot[0x3E:0x1FE] = floppy[0x3E:0x1FE]
    boot[0x1FE:0x200] = b'\x55\xAA'
    f.seek(0)
    f.write(boot)
EOF

# IO.SYS and MSDOS.SYS must be the first two root entries, and IO.SYS
# contiguous: copy them first onto the empty file system.
for f in IO.SYS MSDOS.SYS COMMAND.COM; do
	"$MTOOLS_BIN/mcopy" -i "$DISK1" "::/$f" "$tmp/$f"
	"$MTOOLS_BIN/mcopy" -i "$tmp/part.img" "$tmp/$f" "::/$f"
done
"$MTOOLS_BIN/mattrib" -i "$tmp/part.img" +s +h +r ::/IO.SYS ::/MSDOS.SYS

# ---- Disk: MBR + partition table + partition ------------------------------
python3 - "$SYSLINUX_MBR" "$tmp/part.img" "$OUT" $CYLS $HEADS $SECTORS $PART_START $PART_SIZE <<'EOF'
import struct, sys
mbr_code, part, out = sys.argv[1], sys.argv[2], sys.argv[3]
cyls, heads, sectors, start, size = map(int, sys.argv[4:9])

def chs(lba):
    c = lba // (heads * sectors)
    h = (lba // sectors) % heads
    s = lba % sectors + 1
    c = min(c, 1023)
    return bytes([h, ((c >> 2) & 0xC0) | s, c & 0xFF])

mbr = bytearray(512)
mbr[:440] = open(mbr_code, 'rb').read()[:440]
# Active FAT16 (>= 32 MB) partition.
entry = bytes([0x80]) + chs(start) + bytes([0x06]) + chs(start + size - 1) + struct.pack('<II', start, size)
mbr[446:462] = entry
mbr[510:512] = b'\x55\xAA'

with open(out, 'wb') as f:
    f.write(mbr)
    f.write(b'\0' * 512 * (start - 1))
    f.write(open(part, 'rb').read())
    total = cyls * heads * sectors * 512
    f.truncate(total)
EOF

echo "Wrote $OUT ($CYLS/$HEADS/$SECTORS, partition at LBA $PART_START)"
echo "Copy files in with: MTOOLS_SKIP_CHECK=1 mcopy -i $OUT@@$((PART_START * 512)) <file> ::/"
