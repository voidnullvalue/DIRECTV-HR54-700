#!/bin/sh
set -eu
test "${1:-}" = I_UNDERSTAND_ONLY_CONFIRMED_PARTITION_2 || { echo "usage: $0 I_UNDERSTAND_ONLY_CONFIRMED_PARTITION_2" >&2; exit 2; }
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VM="$BASE/mips-vm"
EXPECTED_DISK_SIZE=1000204886016
# Serial of YOUR HR54 disk. Read it with:
#   lsblk -dno NAME,SIZE,SERIAL   or   sudo hdparm -I /dev/sdX | grep -i serial
# It is matched together with the size to identify the disk unambiguously.
EXPECTED_SERIAL="${HR54_DISK_SERIAL:-REPLACE_WITH_YOUR_DISK_SERIAL}"
[ "$EXPECTED_SERIAL" != REPLACE_WITH_YOUR_DISK_SERIAL ] || { echo "REFUSED: set HR54_DISK_SERIAL to your own disk serial" >&2; exit 2; }
EXPECTED_P1_SIZE=542835712
EXPECTED_P2_SIZE=16113320448
EXPECTED_P3_SIZE=983406247936
EXPECTED_P4_SIZE=139829760
EXPECTED_P1_START=64
EXPECTED_P2_START=1060296
EXPECTED_P3_START=32531632
EXPECTED_P4_START=1953246960

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
test "$(printf '%s\n' "$DISK_NAME" | sed '/^$/d' | wc -l)" -eq 1 || die "expected exactly one matching disk, got: $DISK_NAME"
DISK="/dev/$DISK_NAME"
DEV=$(lsblk -nrpo NAME,PARTN "$DISK" | awk '$2 == 2 {print $1}')
test "$(printf '%s\n' "$DEV" | sed '/^$/d' | wc -l)" -eq 1 || die "could not uniquely resolve partition 2"
cleanup() { sucmd "blockdev --setro $DISK" >/dev/null 2>&1 || true; }
trap cleanup EXIT HUP INT TERM

test "$(sucmd "blockdev --getsize64 $DISK")" -eq "$EXPECTED_DISK_SIZE" || die "disk size mismatch"
for N in 1 2 3 4; do
    PART=$(lsblk -nrpo NAME,PARTN "$DISK" | awk -v n="$N" '$2 == n {print $1}')
    test -n "$PART" || die "partition $N not found"
    eval EXPECTED_SIZE=\$EXPECTED_P${N}_SIZE
    eval EXPECTED_START=\$EXPECTED_P${N}_START
    test "$(sucmd "blockdev --getsize64 $PART")" -eq "$EXPECTED_SIZE" || die "partition $N size mismatch"
    test "$(lsblk -nbo START "$PART")" -eq "$EXPECTED_START" || die "partition $N start mismatch"
    findmnt -rn -S "$PART" >/dev/null && die "$PART already mounted"
done
findmnt -rn -S "$DISK" >/dev/null && die "$DISK already mounted"
test "$(sucmd "blockdev --getro $DISK")" -eq 1 || die "disk not initially read-only"

"$BASE/verify-offline-v3.sh" > "$BASE/offline-verification-v3-at-install.txt"
grep -q '^OFFLINE_VERIFY_V3=PASS$' "$BASE/offline-verification-v3-at-install.txt" || die "offline v3 gate failed"
"$VM/build-v3-initrds.sh" > "$VM/build-v3-initrds.log"

sucmd "blockdev --setrw $DISK"
test "$(sucmd "blockdev --getro $DEV")" -eq 0 || die "confirmed partition 2 is not writable"
set +e
sucmd "qemu-system-mips -M malta -m 512 -nographic -no-reboot \
  -kernel $VM/vmlinux-4.19.0-21-4kc-malta \
  -initrd $VM/hr54-v3-install-initrd.gz \
  -append 'console=ttyS0 hr54_v3_install=YES' \
  -drive file=$DEV,format=raw,if=virtio,cache=none,aio=threads \
  -drive file=$BASE/../sdb3-rt-placeholder.img,format=raw,if=virtio,cache=none,aio=threads" | tee "$BASE/mips-vm-v3-install-console.log"
QRC=$?
set -e
grep -q '^HR54_V3_INSTALL_SUCCESS' "$BASE/mips-vm-v3-install-console.log" || die "v3 guest install failed (qemu rc=$QRC)"

sucmd "blockdev --setro $DISK"
test "$(sucmd "blockdev --getro $DISK")" -eq 1 || die "failed to restore disk read-only"
set +e
sucmd "qemu-system-mips -M malta -m 512 -nographic -no-reboot \
  -kernel $VM/vmlinux-4.19.0-21-4kc-malta \
  -initrd $VM/hr54-v3-check-initrd.gz \
  -append 'console=ttyS0 hr54_v3_check=YES' \
  -drive file=$DEV,format=raw,if=virtio,cache=none,aio=threads,readonly=on \
  -drive file=$BASE/../sdb3-rt-placeholder.img,format=raw,if=virtio,cache=none,aio=threads" | tee "$BASE/mips-vm-v3-check-console.log"
QRC=$?
set -e
grep -q '^HR54_V3_READONLY_VERIFY_SUCCESS' "$BASE/mips-vm-v3-check-console.log" || die "v3 read-only verification failed (qemu rc=$QRC)"
test "$(sucmd "blockdev --getro $DISK")" -eq 1 || die "disk not read-only after verification"
trap - EXIT HUP INT TERM
echo "V3 beachhead installed only on confirmed $DEV; hashes verified read-only; $DISK is read-only."
