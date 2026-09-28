#!/usr/bin/env python3
"""Point the stock Help tile labels at the existing appended Jellyfin strings."""
import argparse
import hashlib
import struct
from pathlib import Path
from inspect_settings import Buffer, sections

def section_tables(path):
    buf = Buffer(path)
    root = buf.u32(0)
    response = buf.vector(buf.field(root, 0))[0]
    page = buf.target(buf.field(response, 0))
    for section in buf.vector(buf.field(page, 2)):
        block = buf.vector(buf.field(section, 1))[0]
        yield buf.string(buf.field(section, 0)), buf.target(buf.field(block, 2))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    original = args.source.read_bytes()
    data = bytearray(original)
    buf = Buffer(args.source)
    tables = dict(section_tables(args.source))
    help_label = tables["STB_Settings_Help"]
    jellyfin_label = tables["STB_Settings_Jellyfin"]
    for index in (0, 1):
        help_field = buf.field(help_label, index)
        target = buf.target(buf.field(jellyfin_label, index))
        if target <= help_field:
            raise ValueError("replacement string is not forward-addressable")
        struct.pack_into("<I", data, help_field, target - help_field)
    args.output.write_bytes(data)
    help_record = [r for r in sections(args.output) if r[0] == "STB_Settings_Help"][0]
    if help_record[2] != "Jellyfin":
        raise RuntimeError("Help label validation failed")
    print("original", hashlib.sha256(original).hexdigest())
    print("patched ", hashlib.sha256(data).hexdigest())

if __name__ == "__main__":
    main()
