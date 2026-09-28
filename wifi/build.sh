#!/bin/sh
# build nvpre.so for the HR54.
#
# The box runs uClibc; we cross-compile with musl.  Two wrinkles:
#   - -nostdlib: we must not gain a DT_NEEDED on musl's libc.so, which the
#     receiver does not have.  Undefined symbols (write, NVGetObject) resolve at
#     load time against the host process's uClibc and the NVRAM manager.
#   - the box's libDtvNVRamMgr.so has no SONAME, so the linker bakes our local
#     path into DT_NEEDED; rewrite it to the bare name so RUNPATH resolves it.
set -eu
ZIG="${ZIG:?set ZIG to the zig binary, e.g. /opt/zig/zig}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="${LIB:?set LIB to a dir holding your copy of libDtvNVRamMgr.so}"
OUT="$HERE/nvpre.so"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cp "$LIB/libDtvNVRamMgr.so" "$WORK/"
"$ZIG" cc -target mips-linux-musleabi -mcpu=mips32 -shared -fPIC -O2 -nostdlib \
    "$HERE/nvpre.c" -o "$WORK/nvpre.so" \
    -L"$WORK" -lDtvNVRamMgr -Wl,-rpath,/opt/nvram/lib
strip "$WORK/nvpre.so" || true
python3 - "$WORK/nvpre.so" "$OUT" "$WORK" <<'PY'
import sys
src, dst, work = sys.argv[1], sys.argv[2], sys.argv[3]
d = bytearray(open(src, 'rb').read())
full = (work + '/libDtvNVRamMgr.so').encode()
bare = b'libDtvNVRamMgr.so'
i = d.find(full)
if i >= 0:
    # the linker baked our local -L path in; rewrite it to the bare name so
    # RUNPATH resolves it on the receiver
    d[i:i + len(full)] = bare + b'\0' * (len(full) - len(bare))
    open(dst, 'wb').write(d)
    print('patched DT_NEEDED at 0x%x -> %s' % (i, bare.decode()))
elif d.find(bare) >= 0:
    # the linker already recorded the bare name; nothing to do
    open(dst, 'wb').write(d)
    print('DT_NEEDED already bare (%s)' % bare.decode())
else:
    sys.exit('DT_NEEDED for libDtvNVRamMgr.so not found at all; '
             'link probably dropped the dependency')
PY
ls -l "$OUT"
