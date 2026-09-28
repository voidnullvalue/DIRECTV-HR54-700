#!/bin/sh
set -eu
test "${1:-}" = I_UNDERSTAND_READONLY || { echo "usage: $0 I_UNDERSTAND_READONLY" >&2; exit 2; }
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
LOG="$BASE/mips-vm-check-console.log"

die() { echo "REFUSED: $*" >&2; exit 1; }
cleanup() { sucmd "blockdev --setro $DISK" >/dev/null 2>&1 || true; }
trap cleanup EXIT HUP INT TERM

# Privileged helper. Supply the sudo password out of band -- never commit it.
# Prefer running this script under `sudo` with NOPASSWD, otherwise provide it via:
#   export HR54_SUDO_PASS='...'            # from a password manager
#   install -m 600 /dev/null ~/.hr54-sudo-pass && $EDITOR ~/.hr54-sudo-pass
# (that file is gitignored). The credential is read from stdin and never logged.
: "${HR54_SUDO_PASS:=$(cat "${HR54_SUDO_PASS_FILE:-$HOME/.hr54-sudo-pass}" 2>/dev/null)}"
test -n "${HR54_SUDO_PASS:-}" || { echo "REFUSED: no sudo password. Set HR54_SUDO_PASS or write ~/.hr54-sudo-pass (mode 600)." >&2; exit 2; }
sucmd() { printf '%s
' "$HR54_SUDO_PASS" | sudo -S -p '' "$1" 2>/dev/null; }

test "$DEV" = /dev/sdb2 || die "target changed"
test "$(readlink -f "$DEV")" = /dev/sdb2 || die "resolved target changed"
test "$(lsblk -ndo PKNAME "$DEV")" = sdb || die "parent changed"
test "$(sucmd 'blockdev --getsize64 /dev/sdb2')" -eq "$EXPECTED_SIZE" || die "size mismatch"
SERIAL=$(sucmd "udevadm info --query=property --name=/dev/sdb | sed -n 's/^ID_SERIAL_SHORT=//p'")
test "$SERIAL" = "$EXPECTED_SERIAL" || die "serial mismatch: $SERIAL"
for D in /dev/sdb /dev/sdb1 /dev/sdb2 /dev/sdb3 /dev/sdb4; do
    findmnt -rn -S "$D" >/dev/null && die "$D already mounted"
done
sucmd "blockdev --setro $DISK"
test "$(sucmd "blockdev --getro $DISK")" -eq 1 || die "disk not read-only"

# Both drives are attached READ-ONLY. Physical sdb3 is never exposed; vdb
# is only the sparse placeholder file.
set +e
sucmd "qemu-system-mips -M malta -m 512 -nographic -no-reboot \
  -kernel $VM/vmlinux-4.19.0-21-4kc-malta \
  -initrd $VM/hr54-check-initrd.gz \
  -append 'console=ttyS0 hr54_check=YES' \
  -drive file=$DEV,format=raw,if=virtio,cache=none,aio=threads,readonly=on \
  -drive file=$BASE/../sdb3-rt-placeholder.img,format=raw,if=virtio,cache=none,aio=threads" \
  | tee "$LOG"
QRC=$?
set -e
grep -q '^HR54_CHECK_DONE' "$LOG" || die "guest did not report completion (qemu rc=$QRC)"
sucmd "blockdev --setro $DISK"
test "$(sucmd "blockdev --getro $DISK")" -eq 1 || die "disk left writable"
trap - EXIT HUP INT TERM
echo "Disk confirmed read-only after check."
