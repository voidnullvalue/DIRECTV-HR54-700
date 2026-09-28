#!/bin/bash
set -eu
root=${1:?usage: elf_inventory.sh ROOT [OUTPUT_TSV]}
out=${2:-elf-inventory.tsv}
printf 'path\tclass\tendian\tmachine\ttype\tinterpreter\tneeded\n' > "$out"
find "$root" -type f -print0 | while IFS= read -r -d '' f; do
    file -b "$f" | grep -q '^ELF ' || continue
    hdr=$(readelf -h "$f" 2>/dev/null || true)
    cls=$(printf '%s\n' "$hdr" | sed -n 's/^[[:space:]]*Class:[[:space:]]*//p')
    endian=$(printf '%s\n' "$hdr" | sed -n 's/^[[:space:]]*Data:[[:space:]]*//p')
    machine=$(printf '%s\n' "$hdr" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')
    type=$(printf '%s\n' "$hdr" | sed -n 's/^[[:space:]]*Type:[[:space:]]*//p')
    interp=$(readelf -l "$f" 2>/dev/null | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')
    needed=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | paste -sd, -)
    rel=${f#"$root"/}
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$rel" "$cls" "$endian" "$machine" "$type" "$interp" "$needed"
done >> "$out"
