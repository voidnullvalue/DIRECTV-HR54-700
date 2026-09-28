#!/bin/sh
set -eu

test "${1:-}" = RESTORE_ENTIRE_SDB2_FROM_AUTHORITY || { echo "usage: $0 RESTORE_ENTIRE_SDB2_FROM_AUTHORITY" >&2; exit 2; }
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$BASE/.." && pwd)
SRC="$TOP/raw/sdb2.img"
TARGET=/dev/sdb2
DISK=/dev/sdb
MAP="$BASE/restore-sdb2.map"
EXPECTED_SIZE=16113320448
# Serial of YOUR HR54 disk. Read it with:
#   lsblk -dno NAME,SIZE,SERIAL   or   sudo hdparm -I /dev/sdX | grep -i serial
# It is matched together with the size to identify the disk unambiguously.
EXPECTED_SERIAL="${HR54_DISK_SERIAL:-REPLACE_WITH_YOUR_DISK_SERIAL}"
[ "$EXPECTED_SERIAL" != REPLACE_WITH_YOUR_DISK_SERIAL ] || { echo "REFUSED: set HR54_DISK_SERIAL to your own disk serial" >&2; exit 2; }
EXPECTED_SHA256=1b93be57da0aa5a07eb54bbf900a3bb6d778e0c1a82aee4472223e304737c0ad

die() { echo "REFUSED: $*" >&2; exit 1; }
test "$TARGET" = /dev/sdb2 || die "literal target changed"
case "$TARGET" in /dev/sdb|/dev/sdb1|/dev/sdb3|/dev/sdb4) die "forbidden target";; esac
test "$(readlink -f "$TARGET")" = /dev/sdb2 || die "resolved target mismatch"
test "$(lsblk -ndo PKNAME "$TARGET")" = sdb || die "parent mismatch"
test "$(sudo blockdev --getsize64 "$TARGET")" -eq "$EXPECTED_SIZE" || die "target size mismatch"
test "$(stat -c %s "$SRC")" -eq "$EXPECTED_SIZE" || die "source size mismatch"
SERIAL=$(sudo udevadm info --query=property --name="$DISK" | sed -n 's/^ID_SERIAL_SHORT=//p')
test "$SERIAL" = "$EXPECTED_SERIAL" || die "serial mismatch: $SERIAL"
ACTUAL=$(sha256sum "$SRC" | awk '{print $1}')
test "$ACTUAL" = "$EXPECTED_SHA256" || die "authoritative image hash mismatch: $ACTUAL"
mountpoint -q "$TARGET" && die "target appears mounted"
sudo blockdev --setrw "$DISK"
sudo ddrescue -f "$SRC" "$TARGET" "$MAP"
sync
sudo blockdev --setro "$DISK"
test "$(sudo blockdev --getro "$DISK")" -eq 1 || die "failed to restore disk read-only"
sudo xfs_info "$TARGET" >"$BASE/restored-xfs-info.txt"
blkid -p "$TARGET" >"$BASE/restored-blkid.txt"
echo "Restored exactly /dev/sdb2 from authoritative image; metadata recorded; disk is read-only."
