#!/bin/sh
set -eu
test "${1:-}" = I_UNDERSTAND_READONLY || { echo "usage: $0 I_UNDERSTAND_READONLY" >&2; exit 2; }
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VM="$BASE/mips-vm"
EXPECTED_DISK_SIZE=1000204886016
EXPECTED_PART_SIZE=16113320448
# Serial of YOUR HR54 disk. Read it with:
#   lsblk -dno NAME,SIZE,SERIAL   or   sudo hdparm -I /dev/sdX | grep -i serial
# It is matched together with the size to identify the disk unambiguously.
EXPECTED_SERIAL="${HR54_DISK_SERIAL:-REPLACE_WITH_YOUR_DISK_SERIAL}"
[ "$EXPECTED_SERIAL" != REPLACE_WITH_YOUR_DISK_SERIAL ] || { echo "REFUSED: set HR54_DISK_SERIAL to your own disk serial" >&2; exit 2; }
EXPECTED_P1_START=64
EXPECTED_P2_START=1060296
EXPECTED_P3_START=32531632
EXPECTED_P4_START=1953246960
LOG="$BASE/mips-vm-evidence-console.log"

die() { echo "REFUSED: $*" >&2; exit 1; }
# Privileged helper. Supply the sudo password out of band -- never commit it.
# Prefer running this script under `sudo` with NOPASSWD, otherwise provide it via:
#   export HR54_SUDO_PASS='...'            # from a password manager
#   install -m 600 /dev/null ~/.hr54-sudo-pass && $EDITOR ~/.hr54-sudo-pass
# (that file is gitignored). The credential is read from stdin and never logged.
: "${HR54_SUDO_PASS:=$(cat "${HR54_SUDO_PASS_FILE:-$HOME/.hr54-sudo-pass}" 2>/dev/null)}"
test -n "${HR54_SUDO_PASS:-}" || { echo "REFUSED: no sudo password. Set HR54_SUDO_PASS or write ~/.hr54-sudo-pass (mode 600)." >&2; exit 2; }
sucmd() { printf '%s
' "$HR54_SUDO_PASS" | sudo -S -p '' "$1" 2>/dev/null; }

DISK_NAME=$(lsblk -dnbo NAME,SIZE,SERIAL | awk -v size="$EXPECTED_DISK_SIZE" -v serial="$EXPECTED_SERIAL" '$2 == size && $3 == serial {print $1}')
test "$(printf '%s\n' "$DISK_NAME" | sed '/^$/d' | wc -l)" -eq 1 || die "expected exactly one matching physical disk, got: $DISK_NAME"
DISK="/dev/$DISK_NAME"
DEV="${DISK}2"
cleanup() { sucmd "blockdev --setro $DISK" >/dev/null 2>&1 || true; }
trap cleanup EXIT HUP INT TERM

test "$(sucmd "blockdev --getsize64 $DISK")" -eq "$EXPECTED_DISK_SIZE" || die "disk size mismatch"
test "$(sucmd "blockdev --getsize64 $DEV")" -eq "$EXPECTED_PART_SIZE" || die "partition 2 size mismatch"
test "$(lsblk -ndo PKNAME "$DEV")" = "$DISK_NAME" || die "partition parent mismatch"
test "$(lsblk -nbo START "${DISK}1")" -eq "$EXPECTED_P1_START" || die "partition 1 start mismatch"
test "$(lsblk -nbo START "${DISK}2")" -eq "$EXPECTED_P2_START" || die "partition 2 start mismatch"
test "$(lsblk -nbo START "${DISK}3")" -eq "$EXPECTED_P3_START" || die "partition 3 start mismatch"
test "$(lsblk -nbo START "${DISK}4")" -eq "$EXPECTED_P4_START" || die "partition 4 start mismatch"
for D in "$DISK" "${DISK}1" "${DISK}2" "${DISK}3" "${DISK}4"; do
    findmnt -rn -S "$D" >/dev/null && die "$D already mounted"
done
sucmd "blockdev --setro $DISK"
test "$(sucmd "blockdev --getro $DISK")" -eq 1 || die "disk not read-only"

set +e
sucmd "qemu-system-mips -M malta -m 512 -nographic -no-reboot \
  -kernel $VM/vmlinux-4.19.0-21-4kc-malta \
  -initrd $VM/hr54-evidence-initrd.gz \
  -append 'console=ttyS0 hr54_check=YES' \
  -drive file=$DEV,format=raw,if=virtio,cache=none,aio=threads,readonly=on \
  -drive file=$BASE/../sdb3-rt-placeholder.img,format=raw,if=virtio,cache=none,aio=threads" \
  | tee "$LOG"
QRC=$?
set -e
grep -q '^HR54_EVIDENCE_DONE' "$LOG" || die "guest did not report completion (qemu rc=$QRC)"
sucmd "blockdev --setro $DISK"
test "$(sucmd "blockdev --getro $DISK")" -eq 1 || die "disk left writable"
trap - EXIT HUP INT TERM
echo "Disk $DISK confirmed read-only after evidence collection."
