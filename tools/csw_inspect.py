#!/usr/bin/env python3
"""Structural inspector for the observed DIRECTV/Pace CSW v4/v5 containers.

This parser is deliberately non-extracting.  It validates declared ranges, locates
known payload magics, reads SquashFS sizing metadata, and decodes the fixed 0x80
byte signature trailer without trusting package-supplied lengths.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path


def be32(data, off):
    return struct.unpack_from(">I", data, off)[0]


def cstr(data, off, size):
    return data[off:off + size].split(b"\0", 1)[0].decode("ascii", "replace")


def der_signature(trailer, absolute):
    for i in range(len(trailer) - 8):
        if trailer[i] != 0x30 or i + 2 + trailer[i + 1] > len(trailer):
            continue
        end = i + 2 + trailer[i + 1]
        p = i + 2
        vals = []
        try:
            for _ in range(2):
                if trailer[p] != 2:
                    raise ValueError
                n = trailer[p + 1]
                vals.append(int.from_bytes(trailer[p + 2:p + 2 + n], "big"))
                p += 2 + n
        except (IndexError, ValueError):
            continue
        if p == end:
            return {"offset": absolute + i, "length": end - i,
                    "r_hex": f"{vals[0]:x}", "s_hex": f"{vals[1]:x}"}
    return None


def squashfs(data, off):
    # Both real samples are little-endian SquashFS v4.
    if data[off:off + 4] != b"hsqs" or off + 96 > len(data):
        return None
    return {"offset": off, "inode_count": struct.unpack_from("<I", data, off + 4)[0],
            "created_unix": struct.unpack_from("<I", data, off + 8)[0],
            "block_size": struct.unpack_from("<I", data, off + 12)[0],
            "compression_id": struct.unpack_from("<H", data, off + 20)[0],
            "major": struct.unpack_from("<H", data, off + 28)[0],
            "minor": struct.unpack_from("<H", data, off + 30)[0],
            "bytes_used": struct.unpack_from("<Q", data, off + 40)[0]}


def inspect(path):
    data = path.read_bytes()
    version = be32(data, 0)
    out = {"path": str(path), "actual_size": len(data), "format_version": version,
           "sha256": hashlib.sha256(data).hexdigest(), "fields": {}, "payloads": []}
    if version == 4:
        out["fields"] = {"header_or_metadata_length": be32(data, 4),
                         "declared_total_size": be32(data, 8),
                         "alignment": be32(data, 12),
                         "payload_count": struct.unpack_from(">H", data, 16)[0],
                         "build_id": cstr(data, 18, 66), "manufacturer": cstr(data, 0x54, 64),
                         "model": cstr(data, 0x94, 64), "format_string": cstr(data, 0xd4, 64),
                         "software_version": cstr(data, 0x114, 64),
                         "target_version": cstr(data, 0x154, 64),
                         "target_version_binary": be32(data, 0x198)}
    elif version == 5:
        out["fields"] = {"declared_total_size": be32(data, 4),
                         "payload_1_offset": be32(data, 8), "payload_1_length": be32(data, 12),
                         "payload_2_offset": be32(data, 16), "payload_2_length": be32(data, 20),
                         "image_count_or_flags": be32(data, 24), "model": cstr(data, 0x1c, 64),
                         "build_id": cstr(data, 0x5c, 64), "target_version": cstr(data, 0x9c, 68),
                         "target_version_binary": be32(data, 0xe0),
                         "manufacturer": cstr(data, 0xe4, 64), "field_0x124": be32(data, 0x124),
                         "field_0x128": be32(data, 0x128), "format_string": cstr(data, 0x12c, 64),
                         "software_version": cstr(data, 0x16c, 64)}
        for n in (1, 2):
            off, size = out["fields"][f"payload_{n}_offset"], out["fields"][f"payload_{n}_length"]
            out["payloads"].append({"index": n, "offset": off, "declared_length": size,
                                    "end": off + size, "in_bounds": off + size <= len(data) - 128,
                                    "sha256": hashlib.sha256(data[off:off + size]).hexdigest()})
    else:
        out["error"] = "unsupported CSW version"

    first_squash = data.find(b"hsqs", 0, len(data) - 128)
    for magic, name in ((b"hsqs", "squashfs-le"), (b"\x5d\x00\x00\x80\x00", "lzma"),
                        (b"\xfd7zXZ\x00", "xz"), (b"\xd0\x0d\xfe\xed", "dtb"),
                        (b"\x7fELF", "elf")):
        start = 0
        while True:
            # Compression magics occur naturally in compressed SquashFS blocks;
            # only scan the pre-rootfs area for top-level kernel payloads.
            limit = first_squash if first_squash >= 0 and name in ("lzma", "xz", "dtb", "elf") else len(data) - 128
            pos = data.find(magic, start, limit)
            if pos < 0:
                break
            item = {"type": name, "offset": pos}
            if name == "squashfs-le":
                item.update(squashfs(data, pos) or {})
            out["payloads"].append(item)
            start = pos + 1
    trailer_off = len(data) - 128
    out["signature_trailer"] = {"offset": trailer_off, "length": 128,
                                "leading_zero_bytes": len(data[trailer_off:]) - len(data[trailer_off:].lstrip(b"\0")),
                                "der": der_signature(data[trailer_off:], trailer_off)}
    out["checks"] = {
        "declared_size_matches": out["fields"].get("declared_total_size") == len(data),
        "trailer_is_outside_v5_payloads": version != 5 or all(
            p.get("end", 0) <= trailer_off for p in out["payloads"] if "index" in p),
    }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("packages", nargs="+", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    results = [inspect(p) for p in args.packages]
    if args.json:
        print(json.dumps(results, indent=2))
        return
    for result in results:
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
