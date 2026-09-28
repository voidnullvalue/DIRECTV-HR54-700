#!/bin/bash
set -eu
outdir=${1:?usage: image_scan.sh OUTPUT_DIR IMAGE...}
shift
mkdir -p "$outdir"
for image in "$@"; do
    base=$(basename "$image")
    file "$image" > "$outdir/$base.file.txt"
    binwalk "$image" > "$outdir/$base.binwalk.txt" 2>&1 || true
    strings -a -n 8 "$image" | rg -i 'CFE|bootargs|root=|squashfs|firmware|signature|secure.?boot|BEGIN CERTIFICATE|DIRECTV|Broadcom' > "$outdir/$base.interesting-strings.txt" || true
done
