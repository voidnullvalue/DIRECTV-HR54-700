#!/usr/bin/env python3
"""Find an RSA modulus and its fingerprints byte-for-byte in an artifact tree."""

import argparse
import hashlib
from pathlib import Path


def patterns(modulus_hex: str, fingerprints: list[str]):
    raw = bytes.fromhex(modulus_hex)
    yield "modulus-be", raw
    yield "modulus-be-leading-zero", b"\x00" + raw
    for fp in fingerprints:
        compact = fp.replace(":", "").lower()
        yield f"fingerprint-binary-{len(compact) * 4}", bytes.fromhex(compact)
        yield f"fingerprint-hex-lower-{len(compact) * 4}", compact.encode()
        yield f"fingerprint-hex-upper-{len(compact) * 4}", compact.upper().encode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", type=Path)
    ap.add_argument("--modulus-file", required=True, type=Path)
    ap.add_argument("--fingerprint", action="append", default=[])
    args = ap.parse_args()
    text = args.modulus_file.read_text(errors="replace")
    modulus = text.split("Modulus=", 1)[-1].strip().splitlines()[0]
    needles = list(patterns(modulus, args.fingerprint))
    for path in sorted(args.root.rglob("*")):
        if not path.is_file():
            continue
        try:
            data = path.read_bytes()
        except OSError:
            continue
        for label, needle in needles:
            start = 0
            while True:
                offset = data.find(needle, start)
                if offset < 0:
                    break
                print(f"{path}\t0x{offset:x}\t{label}")
                start = offset + 1


if __name__ == "__main__":
    main()
