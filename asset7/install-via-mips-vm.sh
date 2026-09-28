#!/bin/sh
set -eu
test "${1:-}" = I_UNDERSTAND_ONLY_SDB2 || { echo "usage: $0 I_UNDERSTAND_ONLY_SDB2" >&2; exit 2; }
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VM="$BASE/mips-vm"
DISK=/dev/sdb
DEV=/dev/sdb2
EXPECTED_SIZE=16113320448
# Serial of YOUR HR54 disk. Read it with:
#   lsblk -dno NAME,SIZE,SERIAL   or   sudo hdparm -I /dev/sdX | grep -i serial
# It is matched together with the size to identify the disk unambiguously.
EXPECTED_SERIAL="${HR54_DISK_SERIAL:-REPLACE_WITH_YOUR_DISK_SERIAL}"
[ "$EXPECTED_SERIAL" != REPLACE_WITH_YOUR_DISK_SERIAL ] || { echo "REFUSED: set HR54_DISK_SERIAL to your own disk serial" >&2; exit 2; }

die() { echo "REFUSED: $*" >&2; exit 1; }
cleanup() { sudo blockdev --setro "$DISK" 2>/dev/null || true; }
trap cleanup EXIT HUP INT TERM

test "$DEV" = /dev/sdb2 || die "target changed"
test "$(readlink -f "$DEV")" = /dev/sdb2 || die "resolved target changed"
test "$(lsblk -ndo PKNAME "$DEV")" = sdb || die "parent changed"
test "$(sudo blockdev --getsize64 "$DEV")" -eq "$EXPECTED_SIZE" || die "size mismatch"
SERIAL=$(sudo udevadm info --query=property --name="$DISK" | sed -n 's/^ID_SERIAL_SHORT=//p')
test "$SERIAL" = "$EXPECTED_SERIAL" || die "serial mismatch"
for D in /dev/sdb /dev/sdb1 /dev/sdb2 /dev/sdb3 /dev/sdb4; do
    findmnt -rn -S "$D" >/dev/null && die "$D already mounted"
done
test "$(sudo blockdev --getro "$DISK")" -eq 1 || die "disk not initially read-only"
"$BASE/verify-offline.sh" >"$BASE/offline-verification-at-install.txt"
grep -q '^OFFLINE_VERIFY=PASS$' "$BASE/offline-verification-at-install.txt" || die "offline gate failed"
"$VM/build-initrd.sh" >"$VM/build-initrd.log"

sudo blockdev --setrw "$DISK"
test "$(sudo blockdev --getro "$DEV")" -eq 0 || die "sdb2 not writable"

# Only literal /dev/sdb2 is exposed to the guest. /dev/sdb, sdb1, sdb3 and
# sdb4 are absent. The second guest disk is a workspace sparse placeholder.
set +e
sudo qemu-system-mips -M malta -m 512 -nographic -no-reboot \
  -kernel "$VM/vmlinux-4.19.0-21-4kc-malta" \
  -initrd "$VM/hr54-install-initrd.gz" \
  -append 'console=ttyS0 hr54_install=YES' \
  -drive file="$DEV",format=raw,if=virtio,cache=none,aio=threads \
  -drive file="$BASE/../sdb3-rt-placeholder.img",format=raw,if=virtio,cache=none,aio=threads \
  | tee "$BASE/mips-vm-install-console.log"
QRC=$?
set -e
grep -q '^HR54_INSTALL_SUCCESS' "$BASE/mips-vm-install-console.log" || die "guest did not report success (qemu rc=$QRC)"
sync
sudo blockdev --setro "$DISK"
test "$(sudo blockdev --getro "$DISK")" -eq 1 || die "failed to restore read-only"
trap - EXIT HUP INT TERM
echo "Big-endian guest installed the two PoC files through normal XFS writes; disk is read-only."
