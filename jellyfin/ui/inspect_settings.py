#!/usr/bin/env python3
"""Read the stock STB_Settings FlatBuffers section list without vendor code."""

import argparse
import struct
from pathlib import Path


class Buffer:
    def __init__(self, path):
        self.data = path.read_bytes()

    def u32(self, offset):
        return struct.unpack_from("<I", self.data, offset)[0]

    def i32(self, offset):
        return struct.unpack_from("<i", self.data, offset)[0]

    def u16(self, offset):
        return struct.unpack_from("<H", self.data, offset)[0]

    def field(self, table, index):
        vtable = table - self.i32(table)
        if vtable < 0 or vtable + 4 > len(self.data):
            raise ValueError("invalid FlatBuffers vtable")
        slot = vtable + 4 + 2 * index
        if slot + 2 > vtable + self.u16(vtable):
            return None
        offset = self.u16(slot)
        return table + offset if offset else None

    def target(self, field):
        return field + self.u32(field) if field is not None else None

    def string(self, field):
        target = self.target(field)
        if target is None:
            return None
        length = self.u32(target)
        return self.data[target + 4:target + 4 + length].decode("utf-8")

    def vector(self, field):
        target = self.target(field)
        if target is None:
            return []
        count = self.u32(target)
        return [self.target(target + 4 + i * 4) for i in range(count)]


def sections(path):
    buf = Buffer(path)
    root = buf.u32(0)
    response = buf.vector(buf.field(root, 0))[0]
    page = buf.target(buf.field(response, 0))
    for section in buf.vector(buf.field(page, 2)):
        block = buf.vector(buf.field(section, 1))[0]
        props = buf.target(buf.field(block, 0))
        label = buf.target(buf.field(block, 2))
        yield (buf.string(buf.field(section, 0)),
               buf.string(buf.field(block, 1)),
               buf.string(buf.field(label, 0)),
               buf.string(buf.field(props, 3)),
               buf.string(buf.field(props, 13)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    args = parser.parse_args()
    for record in sections(args.file):
        print("\t".join(record))


if __name__ == "__main__":
    main()
