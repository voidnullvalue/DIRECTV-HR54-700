#!/usr/bin/env python3
"""Create a reversible experimental Settings resource with one Jellyfin row.

This copies stock FlatBuffers objects and appends a new vector; it never edits
the source. The new row's selection action is not established by this patch.
"""

import argparse
import hashlib
import struct
from pathlib import Path

from inspect_settings import Buffer, sections


def put_u32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def put_i32(data, offset, value):
    struct.pack_into("<i", data, offset, value)


def replace_string(data, target, old, new):
    old_bytes = old.encode()
    new_bytes = new.encode()
    if len(new_bytes) > len(old_bytes):
        raise ValueError("replacement would move FlatBuffers objects")
    if data[target + 4:target + 4 + len(old_bytes)] != old_bytes:
        raise ValueError(f"stock string mismatch at {target}")
    put_u32(data, target, len(new_bytes))
    data[target + 4:target + 4 + len(old_bytes) + 1] = (
        new_bytes + b"\0" * (len(old_bytes) + 1 - len(new_bytes)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.read_bytes()
    if len(source) != 4144 or source[0:4] != b"\x04\0\0\0":
        parser.error("this experimental patch only supports the recovered HR54 layout")
    original = list(sections(args.source))
    if len(original) != 7 or original[-1][1] != "stb_setting_misc":
        parser.error("unexpected Settings sections")

    # Existing section roots occupy 508..4068. Their labels and block data
    # remain at the same relative offsets when copied as a group.
    old_roots = [3428, 2952, 2472, 1968, 1452, 992, 508]
    vector_start = len(source)
    clone_start = vector_start + 4 + 8 * 4
    copied = bytearray(source[508:4068])
    jellyfin_start = clone_start + len(copied)
    jellyfin = bytearray(source[508:992])
    output = bytearray(source)
    output.extend(b"\0" * (4 + 8 * 4))
    output.extend(copied)
    output.extend(jellyfin)

    # A FlatBuffers vtable may be shared by distant objects. Rebase table
    # vtable offsets that leave the copied range; internal offsets are stable.
    table_roots = []
    src_buf = Buffer(args.source)
    for root in old_roots:
        block = src_buf.vector(src_buf.field(root, 1))[0]
        table_roots.extend([root, block,
                            src_buf.target(src_buf.field(block, 0)),
                            src_buf.target(src_buf.field(block, 2))])
    for old in table_roots:
        old_vtable = old - src_buf.i32(old)
        if not 508 <= old_vtable < 4068:
            new = clone_start + old - 508
            put_i32(output, new, new - old_vtable)
    for old in table_roots[-4:]:  # clone the Misc. Options section
        old_vtable = old - src_buf.i32(old)
        new = jellyfin_start + old - 508
        put_i32(output, new, new - old_vtable)

    # The copied Misc. Options section has enough string storage for these
    # shorter names. Its structure and all internal references are intact.
    for old_target, old_text, new_text in [
        (960, "STB_Settings_Misc_Options", "STB_Settings_Jellyfin"),
        (596, "stb_setting_misc", "stb_jellyfin"),
        (576, "Misc. Options", "Jellyfin"),
        (556, "Misc. Opciones", "Jellyfin"),
    ]:
        replace_string(output, jellyfin_start + old_target - 508,
                       old_text, new_text)

    put_u32(output, vector_start, 8)
    new_roots = [clone_start + root - 508 for root in old_roots]
    new_roots.append(jellyfin_start)
    for index, root in enumerate(new_roots):
        element = vector_start + 4 + 4 * index
        put_u32(output, element, root - element)
    put_u32(output, 48, vector_start - 48)
    args.output.write_bytes(output)
    records = list(sections(args.output))
    if len(records) != 8 or records[-1][0:3] != (
            "STB_Settings_Jellyfin", "stb_jellyfin", "Jellyfin"):
        raise RuntimeError(f"patched FlatBuffers validation failed: {records!r}")
    print("original", hashlib.sha256(source).hexdigest())
    print("patched ", hashlib.sha256(output).hexdigest())


if __name__ == "__main__":
    main()
