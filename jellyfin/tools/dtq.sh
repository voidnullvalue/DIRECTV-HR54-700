#!/usr/bin/env bash
# dtq.sh - send XML lines (stdin) to the HR54 uconn test handler and print replies.
# Each stdin line is one full XML element.
# Usage:  ... | dtq.sh        (one XML element per stdin line)
HOST="${HR54_HOST:-192.0.2.10}"   # 192.0.2.0/24 = RFC 5737 TEST-NET-1, never a real host
[ -n "${HR54_HOST:-}" ] || { echo "set HR54_HOST to the receiver's LAN address" >&2; exit 2; }
PORT="${HR54_PORT:-5777}"
DEST="/var/hr54-transfer/dtq-in"
RAW=$(mktemp)
{
  cat <<'OUTER'
cat > /var/hr54-transfer/dtq-run.sh <<'DTQEOF'
UC=/opt/middleware_core/system/tv/uconntest
while IFS= read -r l; do
  [ -z "$l" ] && continue
  echo "### $l"
  $UC "$l" 2>&1
  echo "### rc=$?"
done < /var/hr54-transfer/dtq-in
DTQEOF
cat > /var/hr54-transfer/dtq-in <<'DTQIN'
OUTER
  cat
  cat <<'OUTER'
DTQIN
sh /var/hr54-transfer/dtq-run.sh
rm -f /var/hr54-transfer/dtq-run.sh /var/hr54-transfer/dtq-in
exit
OUTER
  sleep 3
} | timeout "${HR54_TIMEOUT:-300}" nc -q 3 "$HOST" "$PORT" > "$RAW" 2>/dev/null
python3 - "$RAW" <<'PYEOF'
import sys
raw = open(sys.argv[1]).read()
for ln in raw.splitlines():
    s = ln.strip()
    if s.startswith('cat > /var/hr54-transfer/dtq-') or s.startswith('sh /var/hr54-transfer/dtq-') \
       or s.startswith('rm -f /var/hr54-transfer/dtq-') or s in ('DTQEOF', 'DTQIN', 'OUTER'):
        continue
    if s == '>' or s.startswith('> '):
        continue
    print(ln)
PYEOF
rm -f "$RAW"
