#!/usr/bin/env bash
# hr54-pull-clean <remote-path> <local-dest>
set -uo pipefail
HOST="${HR54_HOST:-192.0.2.10}"   # 192.0.2.0/24 = RFC 5737 TEST-NET-1, never a real host
[ -n "${HR54_HOST:-}" ] || { echo "set HR54_HOST to the receiver's LAN address" >&2; exit 2; }
PORT="${HR54_PORT:-5777}"
REMOTE="$1"; DEST="$2"
RAW=$(mktemp)
{
  printf 'echo ZZZSTARTZZZ\n'
  printf 'gzip -9 -c %s 2>/dev/null | hexdump -v -e \x271/1 "%%02x"\x27\n' "$REMOTE"
  printf '\necho ZZZENDZZZ\n'
  printf 'exit\n'
  sleep 3
} | timeout "${HR54_TIMEOUT:-1800}" nc -q 3 "$HOST" "$PORT" > "$RAW" 2>/dev/null
python3 - "$RAW" "$DEST" <<'PYEOF'
import sys,re,binascii,gzip
raw = open(sys.argv[1]).read()
m = re.search(r'ZZZSTARTZZZ\s*(.*?)\s*ZZZENDZZZ', raw, re.S)
if not m: sys.exit(1)
h = re.sub(r'[^0-9a-f]','', m.group(1)); h=h[:len(h)//2*2]
d = binascii.unhexlify(h)
d = d[d.find(b'\x1f\x8b'):]
for n in range(len(d), max(0,len(d)-100), -1):
    try:
        r = gzip.decompress(d[:n]); break
    except: pass
else: r = b''
open(sys.argv[2],'wb').write(r)
PYEOF
rm -f "$RAW"
