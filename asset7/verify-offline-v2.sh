#!/bin/sh
set -eu

BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$BASE/.." && pwd)
ROOTFS="$TOP/extracted/sdb4-rootfs"
GENUINE="$TOP/corpus/plugins/7_6932_6932.squashfs"
MAL="$BASE/corrected-plugin/7_6933_6840.squashfs"
SIG="$BASE/corrected-plugin/7_6933_6840.sig"
TMP=$(mktemp -d /tmp/hr54-v2-verify.XXXXXX)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

test -f "$GENUINE" && test -f "$MAL" && test -f "$SIG"
test "$(sed -n 's/^IMAGE[[:space:]]*=[[:space:]]*//p' "$SIG")" = /var/network/plugins/7_6932_6932.squashfs

# Exercise the exact production verifier.  Only this temporary host-side copy
# of IMAGE is redirected to the locally recovered byte-identical anchor.
sed "s#^IMAGE = .*#IMAGE = $GENUINE#" "$SIG" >"$TMP/poc.sig"
mkdir -p "$TMP/sigroot/opt/sig/bin" "$TMP/sigroot/lib" "$TMP/sigroot/usr/lib"
cp "$ROOTFS/opt/sig/bin/sigtst" "$TMP/sigroot/opt/sig/bin/"
cp -a "$ROOTFS/lib/." "$TMP/sigroot/lib/"
test ! -d "$ROOTFS/usr/lib" || cp -a "$ROOTFS/usr/lib/." "$TMP/sigroot/usr/lib/"
mkdir -p "$TMP/sigroot/etc"
cp "$ROOTFS/etc/ld.so.cache" "$TMP/sigroot/etc/"
qemu-mips -L "$TMP/sigroot" "$TMP/sigroot/opt/sig/bin/sigtst" "$TMP/poc.sig"

unsquashfs -no-progress -dest "$TMP/extract" "$MAL" >/dev/null
unsquashfs -no-progress -dest "$TMP/genuine" "$GENUINE" >/dev/null
cmp "$TMP/genuine/mp4lib/lib/libmp4lib.so" "$TMP/extract/mp4lib/lib/libmp4lib.so"
file "$TMP/extract/mp4lib/bin/indexer" | grep -q 'ELF 32-bit MSB.*MIPS'
readelf -hW "$TMP/extract/mp4lib/bin/indexer" | grep -q 'o32, mips32'
test "$(stat -c %a "$TMP/extract/mp4lib/bin/indexer")" = 700

# Minimal receiver runtime rooted under TMP.  A private mount namespace makes
# the MIPS process's absolute /opt and /var paths resolve only inside it.
R="$TMP/runtime"
mkdir -p "$R/etc" "$R/lib" "$R/opt" "$R/var"
cp "$ROOTFS/etc/ld.so.cache" "$R/etc/"
cp -a "$ROOTFS/lib/." "$R/lib/"
cp -a "$TMP/extract/mp4lib" "$R/opt/"
cp -a "$TMP/extract/dcdp" "$R/opt/"
cp /usr/bin/qemu-mips "$R/qemu-mips"
set +e
bwrap --bind "$R" / --dev /dev -- /qemu-mips /opt/mp4lib/bin/indexer >"$TMP/indexer.stdout" 2>"$TMP/indexer.stderr"
INDEXER_RC=$?
set -e
test "$INDEXER_RC" -eq 22
test "$(cat "$R/var/HR54_ROOT_PROOF")" = HR54_ROOT_EXEC
grep -q 'Simple IDR frames indexer' "$TMP/indexer.stderr"

G=$(sha256sum "$GENUINE" | awk '{print $1}')
M=$(sha256sum "$MAL" | awk '{print $1}')
S=$(sha256sum "$SIG" | awk '{print $1}')
printf 'production_sigtst_rc=0\nverified_genuine_sha256=%s\ncorrected_malicious_sha256=%s\ncorrected_sig_sha256=%s\nlibmp4lib=unmodified_genuine\nindexer_elf=MIPS32-big-endian-o32\nindexer_rc=%s\nindexer_banner=preserved\npersistent_proof=HR54_ROOT_EXEC\nOFFLINE_VERIFY_V2=PASS\n' "$G" "$M" "$S" "$INDEXER_RC"
