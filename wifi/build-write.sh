#!/bin/sh
set -eu
ZIG="${ZIG:?set ZIG to the zig binary, e.g. /opt/zig/zig}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="${LIB:?set LIB to a dir holding your copy of libDtvNVRamMgr.so}"
case "${MODE:-original}" in
    original) NAME=nvwrite; DEFINE= ;;
    correction) NAME=nvcorrect; DEFINE=-DCORRECTION ;;
    *) echo "MODE must be original or correction" >&2; exit 2 ;;
esac
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$LIB/libDtvNVRamMgr.so" "$WORK/"
"$ZIG" cc -target mips-linux-musleabi -mcpu=mips32 -shared -fPIC -O2 -nostdlib \
    $DEFINE "$HERE/nvwrite.c" -o "$WORK/$NAME.so" -L"$WORK" -lDtvNVRamMgr \
    -Wl,-rpath,/opt/nvram/lib
python3 - "$WORK/$NAME.so" "$HERE/$NAME.so" "$WORK" <<'PY'
import sys
src, dst, work = sys.argv[1:]
d = bytearray(open(src, 'rb').read())
full = (work + '/libDtvNVRamMgr.so').encode()
bare = b'libDtvNVRamMgr.so'
i = d.find(full)
if i >= 0:
    d[i:i+len(full)] = bare + b'\0' * (len(full)-len(bare))
elif d.find(bare) < 0:
    raise SystemExit('missing NVRAM library dependency')
open(dst, 'wb').write(d)
PY
readelf -d "$HERE/$NAME.so" | grep NEEDED
