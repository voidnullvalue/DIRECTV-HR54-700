#!/bin/bash
set -eu
root=${1:?usage: interesting_strings.sh ROOT OUTPUT}
out=${2:?usage: interesting_strings.sh ROOT OUTPUT}
pattern='root|admin|debug|factory|developer|shell|console|telnet|ssh|dropbear|login|password|passwd|enable_debug|maintenance|support|bootargs|secure.?boot|signature|verify|firmware|upgrade|update|\.csw|system\(|popen\(|execve\(|strcpy|sprintf|listen\(|bind\(|HTTP/'
find "$root" -type f -size -32M -print0 | while IFS= read -r -d '' f; do
    hits=$(strings -a -n 5 "$f" 2>/dev/null | rg -i "$pattern" | head -80 || true)
    test -n "$hits" || continue
    printf '===== %s =====\n%s\n' "${f#"$root"/}" "$hits"
done > "$out"
