#!/usr/bin/env bash
# hr54-pull.sh <remote-path> <local-dest>
# Pull a file off the HR54 over the TCP/5777 root shell using gzip + hex framing.
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
python3 - "$RAW" "$DEST" <<'EOF'
import sys, binascii, gzip, re
raw = open(sys.argv[1], 'rb').read().decode('latin1')
m = re.search(r'ZZZSTARTZZZ\s*(.*?)\s*ZZZENDZZZ', raw, re.S)
if not m:
    print("no frame found in %d bytes" % len(raw)); sys.exit(1)
h = re.sub(r'[^0-9a-f]', '', m.group(1))
h = h[:len(h)//2*2]
data = binascii.unhexlify(h)
try:
    data = gzip.decompress(data)
except Exception as e:
    print("gunzip failed:", e, "raw:", len(data))
open(sys.argv[2], 'wb').write(data)
print("wrote %s %d bytes" % (sys.argv[2], len(data)))
EOF
rm -f "$RAW"
