#!/bin/sh
set -eu

BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$BASE/.." && pwd)
ROOTFS="$TOP/extracted/sdb4-rootfs"
GENUINE="$TOP/corpus/plugins/7_6932_6932.squashfs"
MAL="$BASE/beachhead-plugin/7_6933_6840.squashfs"
SIG="$BASE/beachhead-plugin/7_6933_6840.sig"
TMP=$(mktemp -d /tmp/hr54-v3-verify.XXXXXX)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

test -f "$GENUINE" && test -f "$MAL" && test -f "$SIG"
test "$(sed -n 's/^IMAGE[[:space:]]*=[[:space:]]*//p' "$SIG")" = \
    /var/network/plugins/7_6932_6932.squashfs

# Exercise the exact production verifier with IMAGE redirected only in this
# temporary host-side copy to the recovered byte-identical genuine anchor.
sed "s#^IMAGE = .*#IMAGE = $GENUINE#" "$SIG" > "$TMP/poc.sig"
mkdir -p "$TMP/sigroot/opt/sig/bin" "$TMP/sigroot/lib" \
    "$TMP/sigroot/usr/lib" "$TMP/sigroot/etc"
cp "$ROOTFS/opt/sig/bin/sigtst" "$TMP/sigroot/opt/sig/bin/"
cp -a "$ROOTFS/lib/." "$TMP/sigroot/lib/"
test ! -d "$ROOTFS/usr/lib" || cp -a "$ROOTFS/usr/lib/." "$TMP/sigroot/usr/lib/"
cp "$ROOTFS/etc/ld.so.cache" "$TMP/sigroot/etc/"
qemu-mips -L "$TMP/sigroot" "$TMP/sigroot/opt/sig/bin/sigtst" "$TMP/poc.sig"

unsquashfs -no-progress -dest "$TMP/extract" "$MAL" >/dev/null
unsquashfs -no-progress -dest "$TMP/genuine" "$GENUINE" >/dev/null
cmp "$TMP/genuine/mp4lib/lib/libmp4lib.so" \
    "$TMP/extract/mp4lib/lib/libmp4lib.so"
cmp "$TMP/genuine/mp4lib/bin/indexer" \
    "$TMP/extract/mp4lib/bin/indexer.real"
cmp "$TOP/linux-port/beachhead/indexer" \
    "$TMP/extract/mp4lib/bin/indexer"
cmp "$TOP/linux-port/beachhead/collect-inventory" \
    "$TMP/extract/mp4lib/bin/collect-inventory"
cmp "$TOP/linux-port/beachhead/hr54d" \
    "$TMP/extract/mp4lib/bin/hr54d"

file "$TMP/extract/mp4lib/bin/hr54d" | \
    grep -q 'ELF 32-bit MSB.*MIPS.*statically linked'
readelf -hW "$TMP/extract/mp4lib/bin/hr54d" | grep -q 'o32, mips32'
test "$(stat -c %a "$TMP/extract/mp4lib/bin/indexer")" = 700
test "$(stat -c %a "$TMP/extract/mp4lib/bin/indexer.real")" = 700
test "$(stat -c %a "$TMP/extract/mp4lib/bin/hr54d")" = 700
test "$(stat -c %a "$TMP/extract/mp4lib/bin/collect-inventory")" = 700
/bin/sh -n "$TMP/extract/mp4lib/bin/indexer"
/bin/sh -n "$TMP/extract/mp4lib/bin/collect-inventory"

# Validate the launcher under the exact recovered MIPS shell.  The offline
# branch deliberately uses only shell builtins; qemu-user cannot recursively
# exec target binaries here without binfmt registration.
R="$TMP/runtime"
mkdir -p "$R/etc" "$R/lib" "$R/opt" "$R/var"
cp "$ROOTFS/etc/ld.so.cache" "$R/etc/"
cp -a "$ROOTFS/lib/." "$R/lib/"
cp -a "$ROOTFS/bin" "$ROOTFS/sbin" "$ROOTFS/usr" "$R/"
cp -a "$TMP/extract/mp4lib" "$R/opt/"
cp -a "$TMP/extract/dcdp" "$R/opt/"
cp /usr/bin/qemu-mips "$R/qemu-mips"
set +e
bwrap --bind "$R" / --dev /dev --proc /proc --setenv HR54_OFFLINE_TEST 1 \
    -- /qemu-mips /bin/sh /opt/mp4lib/bin/indexer \
    > "$TMP/launcher.stdout" 2> "$TMP/launcher.stderr"
LAUNCHER_RC=$?
set -e
test "$LAUNCHER_RC" -eq 22
test "$(cat "$R/var/HR54_ROOT_PROOF")" = HR54_ROOT_EXEC
test "$(cat "$R/var/HR54_BEACHHEAD_TEST")" = HR54_BEACHHEAD_LAUNCHER_OK

# The preserved executable must retain its original probe behavior.
set +e
bwrap --bind "$R" / --dev /dev --proc /proc -- \
    /qemu-mips /opt/mp4lib/bin/indexer.real \
    > "$TMP/indexer.stdout" 2> "$TMP/indexer.stderr"
INDEXER_RC=$?
set -e
test "$INDEXER_RC" -eq 22
grep -q 'Simple IDR frames indexer' "$TMP/indexer.stderr"

G=$(sha256sum "$GENUINE" | awk '{print $1}')
M=$(sha256sum "$MAL" | awk '{print $1}')
S=$(sha256sum "$SIG" | awk '{print $1}')
D=$(sha256sum "$TMP/extract/mp4lib/bin/hr54d" | awk '{print $1}')
printf 'production_sigtst_rc=0\nverified_genuine_sha256=%s\nbeachhead_malicious_sha256=%s\nbeachhead_sig_sha256=%s\nhr54d_sha256=%s\nlibmp4lib=unmodified_genuine\nindexer_real=unmodified_genuine\nhr54d_elf=MIPS32-big-endian-o32-static\nlauncher_rc=%s\nlauncher_proof=HR54_ROOT_EXEC\nlauncher_marker=HR54_BEACHHEAD_LAUNCHER_OK\nindexer_real_rc=%s\nindexer_real_banner=preserved\nOFFLINE_VERIFY_V3=PASS\n' \
    "$G" "$M" "$S" "$D" "$LAUNCHER_RC" "$INDEXER_RC"
