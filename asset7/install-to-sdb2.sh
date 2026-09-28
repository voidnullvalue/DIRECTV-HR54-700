#!/bin/sh
set -eu

CONFIRM=${1:-}
test "$CONFIRM" = I_UNDERSTAND_ONLY_SDB2 || { echo "usage: $0 I_UNDERSTAND_ONLY_SDB2" >&2; exit 2; }
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$BASE/.." && pwd)
DEV=/dev/sdb2
DISK=/dev/sdb
MNT="$BASE/mnt-sdb2"
MAL="$BASE/malicious-plugin/7_6933_6840.squashfs"
SIG="$BASE/malicious-plugin/7_6933_6840.sig"
EXPECTED_SIZE=16113320448
# Serial of YOUR HR54 disk. Read it with:
#   lsblk -dno NAME,SIZE,SERIAL   or   sudo hdparm -I /dev/sdX | grep -i serial
# It is matched together with the size to identify the disk unambiguously.
EXPECTED_SERIAL="${HR54_DISK_SERIAL:-REPLACE_WITH_YOUR_DISK_SERIAL}"
[ "$EXPECTED_SERIAL" != REPLACE_WITH_YOUR_DISK_SERIAL ] || { echo "REFUSED: set HR54_DISK_SERIAL to your own disk serial" >&2; exit 2; }
RT_PLACEHOLDER="$TOP/sdb3-rt-placeholder.img"
RT_LOOP=

die() { echo "REFUSED: $*" >&2; exit 1; }
cleanup() {
    sync || true
    mountpoint -q "$MNT" && sudo umount "$MNT" || true
    test -z "$RT_LOOP" || sudo losetup -d "$RT_LOOP" 2>/dev/null || true
    sudo blockdev --setro "$DISK" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

test "$DEV" = /dev/sdb2 || die "target is not literal /dev/sdb2"
test "$(readlink -f "$DEV")" = /dev/sdb2 || die "target resolution changed"
test "$(sudo blockdev --getsize64 "$DEV")" -eq "$EXPECTED_SIZE" || die "partition size mismatch"
SERIAL=$(sudo udevadm info --query=property --name="$DISK" | sed -n 's/^ID_SERIAL_SHORT=//p')
test "$SERIAL" = "$EXPECTED_SERIAL" || die "disk serial mismatch: $SERIAL"
test "$(lsblk -ndo PKNAME "$DEV")" = sdb || die "parent disk mismatch"
for D in /dev/sdb /dev/sdb1 /dev/sdb2 /dev/sdb3 /dev/sdb4; do
    findmnt -rn -S "$D" >/dev/null && die "$D is already mounted"
done
test -f "$MAL" && test -f "$SIG" || die "PoC files missing"
"$BASE/verify-offline.sh" | tee "$BASE/offline-verification-at-install.txt"

mkdir -p "$MNT"
test "$(sudo blockdev --getro "$DISK")" -eq 1 || die "disk was not read-only at entry"
sudo blockdev --setrw "$DISK"
test "$(sudo blockdev --getro "$DEV")" -eq 0 || die "could not enable sdb2 writes"

# This XFS has an external realtime device whose extents live on sdb3.  Use a
# sparse zero placeholder only to satisfy XFS geometry; plugin files occupy
# normal data extents on sdb2.  /dev/sdb3 is never opened or mounted.
RT_LOOP=$(sudo losetup -f --show "$RT_PLACEHOLDER")
sudo mount -t xfs -o rw,nouuid,rtdev="$RT_LOOP" "$DEV" "$MNT"
test "$(findmnt -n -o SOURCE --target "$MNT")" = /dev/sdb2 || die "mounted source is not /dev/sdb2"

DEST="$MNT/network/plugins"
test -d "$DEST" || die "plugin directory missing"
find "$DEST" -maxdepth 1 -type f -printf '%f\t%s\t%T@\n' | sort >"$BASE/preinstall-plugin-inventory.tsv"
(cd "$DEST" && sha256sum ./* 2>/dev/null | sort) >"$BASE/preinstall-plugin-sha256.txt"

# Immediately before each ordinary filesystem write, reassert the exact mount.
test "$(findmnt -n -o SOURCE --target "$MNT")" = /dev/sdb2 || die "write guard failed"
sudo install -m 0644 -o root -g root "$MAL" "$DEST/7_6933_6840.squashfs"
test "$(findmnt -n -o SOURCE --target "$MNT")" = /dev/sdb2 || die "write guard failed"
sudo install -m 0644 -o root -g root "$SIG" "$DEST/7_6933_6840.sig"
sync
sha256sum "$DEST/7_6933_6840.squashfs" "$DEST/7_6933_6840.sig" >"$BASE/installed-files-sha256.txt"
cmp "$MAL" "$DEST/7_6933_6840.squashfs"
cmp "$SIG" "$DEST/7_6933_6840.sig"
sudo umount "$MNT"
sudo losetup -d "$RT_LOOP"
RT_LOOP=
sudo blockdev --setro "$DISK"
test "$(sudo blockdev --getro "$DISK")" -eq 1
trap - EXIT HUP INT TERM
echo "Installed only /var/network/plugins/7_6933_6840.{squashfs,sig}; disk returned read-only."
