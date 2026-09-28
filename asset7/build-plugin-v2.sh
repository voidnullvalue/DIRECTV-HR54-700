#!/bin/sh
set -eu

BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$BASE/.." && pwd)
GENUINE="$TOP/corpus/plugins/7_6932_6932.squashfs"
GENUINE_SIG="$TOP/corpus/plugins/7_6932_6932.sig"
WORK="$BASE/corrected-plugin"
ROOT="$WORK/root"
OUT="$WORK/7_6933_6840.squashfs"
SIG="$WORK/7_6933_6840.sig"

test -f "$GENUINE" && test -f "$GENUINE_SIG"
rm -rf "$ROOT"
mkdir -p "$ROOT"
unsquashfs -no-progress -dest "$ROOT" "$GENUINE" >/dev/null

sed -i 's#<version>6932</version>#<version>6933</version>#; s#<min_stack_version>6932</min_stack_version>#<min_stack_version>6840</min_stack_version>#' "$ROOT/config.xml"
sed -i 's/version="6932" min_stack_version="6932"/version="6933" min_stack_version="6840"/' "$ROOT/mp4lib/feature_config.xml" "$ROOT/hls/feature_config.xml" "$ROOT/dcdp/feature_config.xml"

"$BASE/build-indexer-payload.sh" "$ROOT/mp4lib/bin/indexer"
rm -f "$OUT"
find "$ROOT" -exec touch -h -d '@1722973454' {} +
mksquashfs "$ROOT" "$OUT" -noappend -comp lzma -b 131072 -no-tailends -exports -all-root -mkfs-time 1722973454 -all-time 1722973454 -no-progress >/dev/null

SIGNATURE=$(sed -n 's/^SIGNATURE[[:space:]]*=[[:space:]]*//p' "$GENUINE_SIG")
SIZE=$(stat -c %s "$GENUINE")
printf 'IMAGE = /var/network/plugins/7_6932_6932.squashfs\nSIGNATURE = %s\nSIZE = %s\n' "$SIGNATURE" "$SIZE" >"$SIG"

sha256sum "$OUT" "$SIG"
