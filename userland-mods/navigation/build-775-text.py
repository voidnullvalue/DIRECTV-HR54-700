#!/usr/bin/env python3
"""Build a two-entry 775 text overlay from a stock HR54 English resource."""
import argparse
import hashlib
from pathlib import Path

replacements = {
    'S2412 = "775 - No Dish Communication"': 'S2412 = "Press Menu to begin"',
    'S2636 = "There is a problem communicating with the satellite dish (775)"':
        'S2636 = "Press Menu to begin"',
}

parser = argparse.ArgumentParser()
parser.add_argument('stock', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
data = args.stock.read_bytes()
for old, new in replacements.items():
    old_bytes, new_bytes = old.encode(), new.encode()
    if data.count(old_bytes) != 1:
        raise SystemExit(f'Expected exactly one {old!r} entry')
    data = data.replace(old_bytes, new_bytes)
args.output.write_bytes(data)
print('stock SHA256:', hashlib.sha256(args.stock.read_bytes()).hexdigest())
print('patch SHA256:', hashlib.sha256(data).hexdigest())
