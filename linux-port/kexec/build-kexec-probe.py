#!/usr/bin/env python3
"""Build a harmless MIPS o32 kexec_load availability probe.

It invokes kexec_load(entry=0, nr_segments=0, segments=NULL, flags=0).
That request cannot load an image: an implemented kexec syscall must reject it
with EINVAL (22), while an unimplemented syscall returns ENOSYS (38).
The program exits with the raw syscall return value, so no kernel state is
changed.  Shell exit status therefore distinguishes EINVAL (22 or 234 for a
negative errno ABI) from ENOSYS (38 or 218).
"""

from pathlib import Path
import struct

BASE = 0x00400000
OFF = 0x100
ENTRY = BASE + OFF
OUT = Path(__file__).resolve().parent / "kexec-probe"


def i(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


ZERO, V0, A0, A1, A2, A3 = 0, 2, 4, 5, 6, 7

# o32 Linux syscall ABI: v0 is the syscall number.
# Linux 3.3 MIPS o32 defines kexec_load as 4000 + 311 = 4311.
words = [
    i(0x09, ZERO, A0, 0),
    i(0x09, ZERO, A1, 0),
    i(0x09, ZERO, A2, 0),
    i(0x09, ZERO, A3, 0),
    i(0x09, ZERO, V0, 4311),
    0x0000000C,
    ((V0 << 21) | (ZERO << 16) | (A0 << 11) | 0x21),  # addu a0, v0, zero
    i(0x09, ZERO, V0, 4001),
    0x0000000C,
]

code = b"".join(struct.pack(">I", word) for word in words)
body = bytearray(OFF)
body.extend(code)
ident = b"\x7fELF" + bytes([1, 2, 1, 0]) + bytes(8)
body[:52] = struct.pack(
    ">16sHHIIIIIHHHHHH", ident, 2, 8, 1, ENTRY, 52, 0,
    0x50001007, 52, 32, 1, 0, 0, 0,
)
body[52:84] = struct.pack(
    ">IIIIIIII", 1, 0, BASE, BASE, len(body), len(body), 5, 0x1000,
)
OUT.write_bytes(body)
OUT.chmod(0o755)
print(f"{OUT} {len(body)} bytes")
