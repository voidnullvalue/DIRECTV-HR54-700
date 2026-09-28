#!/bin/sh
set -eu

BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$BASE/.." && pwd)
ROOTFS="$TOP/extracted/sdb4-rootfs"
GENUINE="$TOP/corpus/plugins/7_6932_6932.squashfs"
MAL="$BASE/malicious-plugin/7_6933_6840.squashfs"
SIG="$BASE/malicious-plugin/7_6933_6840.sig"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

test -f "$GENUINE" && test -f "$MAL" && test -f "$SIG"
test "$(sed -n 's/^IMAGE[[:space:]]*=[[:space:]]*//p' "$SIG")" = /var/network/plugins/7_6932_6932.squashfs

# IMAGE is unsigned. For the host-side QEMU run only, redirect that field to
# the byte-identical copied genuine artifact available on this host. The
# hardware .sig retains the receiver's /var/network/plugins pathname.
sed "s#^IMAGE = .*#IMAGE = $GENUINE#" "$SIG" >"$TMP/poc.sig"
mkdir -p "$TMP/opt/sig/bin" "$TMP/lib" "$TMP/usr/lib"
cp "$ROOTFS/opt/sig/bin/sigtst" "$TMP/opt/sig/bin/"
cp -a "$ROOTFS/lib/." "$TMP/lib/"
test ! -d "$ROOTFS/usr/lib" || cp -a "$ROOTFS/usr/lib/." "$TMP/usr/lib/"

set +e
qemu-mips -L "$TMP" "$TMP/opt/sig/bin/sigtst" "$TMP/poc.sig"
SIGRC=$?
set -e
test "$SIGRC" -eq 0 || { echo "FAIL: production sigtst rc=$SIGRC" >&2; exit 1; }

G=$(sha256sum "$GENUINE" | awk '{print $1}')
M=$(sha256sum "$MAL" | awk '{print $1}')
test "$G" != "$M"
rm -rf "$TMP/extract"
unsquashfs -no-progress -dest "$TMP/extract" "$MAL" >/dev/null
cmp "$TMP/extract/mp4lib/bin/p" "$BASE/malicious-plugin/root/mp4lib/bin/p"
file "$TMP/extract/mp4lib/lib/libmp4lib.so" | grep -q 'ELF 32-bit MSB.*MIPS'
readelf -h "$TMP/extract/mp4lib/lib/libmp4lib.so" | grep -q 'o32, mips32'
readelf -d "$TMP/extract/mp4lib/lib/libmp4lib.so" | grep -q 'SONAME.*libmp4lib.so'

# Practical command compatibility check using the receiver's exact MIPS
# uClibc BusyBox. Redirect proof paths into TMP so the host is untouched.
sed "s#OUT=/var/tmp/HR54_ROOT_PROOF#OUT=$TMP/HR54_ROOT_PROOF.tmp#; s#PERSIST=/var/HR54_ROOT_PROOF#PERSIST=$TMP/HR54_ROOT_PROOF#" "$TMP/extract/mp4lib/bin/p" >"$TMP/p-test"
chmod 0755 "$TMP/p-test"
qemu-mips -L "$ROOTFS" "$ROOTFS/bin/busybox" ash "$TMP/p-test"
test -s "$TMP/HR54_ROOT_PROOF" && grep -q 'HR54 asset-7 root proof' "$TMP/HR54_ROOT_PROOF"

printf 'production_sigtst_rc=0\nverified_genuine_sha256=%s\nselected_malicious_sha256=%s\nhashes_differ=yes\nsquashfs_extract=yes\npayload_elf=MIPS32-big-endian-o32\nqemu_proof_script=yes\nOFFLINE_VERIFY=PASS\n' "$G" "$M"
