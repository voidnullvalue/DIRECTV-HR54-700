#!/usr/bin/env bash
# hr54-playurl2.sh <outfile> <url> [waitsecs] [extra-xml-attrs] [command]
# Dismiss the Druid screensaver, invoke a DirectTest command, and save the log slice.
set -uo pipefail
HOST="${HR54_HOST:-192.0.2.10}"   # 192.0.2.0/24 = RFC 5737 TEST-NET-1, never a real host
[ -n "${HR54_HOST:-}" ] || { echo "set HR54_HOST to the receiver's LAN address" >&2; exit 2; }; PORT="${HR54_PORT:-5777}"
OUT="$1"; URL="$2"; WAIT="${3:-20}"; EXTRA="${4:-}"; CMD="${5:-playURL}"
SCRIPT=$(mktemp); RAW=$(mktemp)
cat > "$SCRIPT" <<'OUTER'
UC=/opt/middleware_core/system/tv/uconntest
LOG=/var/viewer/messages.log
A="$$HR54EXP-A"
B="$$HR54EXP-B"
echo "@@WAKE@@"
for k in info exit; do
  wget -q -O /dev/null "http://127.0.0.1:8080/remote/processKey?key=$k&hold=keyPress" 2>&1 || \
  { echo "wget missing, trying shell"; }
  sleep 1
done
sleep 2
echo "$A $(date +%H:%M:%S)" >> $LOG
$UC 'REQXML' 2>&1
echo "INVOCATION_RC=$?"
sleep WAITSEC
ps > /var/hr54-transfer/ps.txt
echo "$B $(date +%H:%M:%S)" >> $LOG
sleep 1
echo "@@PS@@"
cat /var/hr54-transfer/ps.txt
echo "@@DELTA@@"
sed -n "/$A/,/$B/p" $LOG
echo "@@END@@"
exit
OUTER
python3 - "$SCRIPT" "$URL" "$WAIT" "$EXTRA" "$CMD" <<'PY'
import sys
p,url,wait,extra,cmd = sys.argv[1:6]
s=open(p).read()
s=s.replace('REQXML','<com.ucentric.pvruconnect.DirectTest command="%s" url="%s"%s/>'%(cmd,url,extra))
s=s.replace('WAITSEC',wait)
open(p,'w').write(s)
PY
{ cat "$SCRIPT"; sleep 3; } | timeout 600 nc -q 3 "$HOST" "$PORT" > "$RAW" 2>/dev/null
python3 - "$RAW" "$OUT" <<'PY'
import sys
raw=open(sys.argv[1],encoding='latin1').read()
lines=raw.splitlines()
def idx(tag):
    for k,l in enumerate(lines):
        if l.strip()==tag or l.endswith(tag): return k
    return None
i=idx('@@DELTA@@'); j=idx('@@END@@'); a=idx('@@PS@@')
head=[l for l in lines[:(a if a is not None else len(lines))] if 'HR54EXP' not in l]
ps=lines[a+1:i] if (a is not None and i is not None) else []
body=lines[i+1:j] if (i is not None and j is not None) else []
with open(sys.argv[2],'w',encoding='latin1') as f:
    f.write("=== INVOCATION\n"); f.write("\n".join(head)+"\n")
    f.write("=== PROCESSES\n");    f.write("\n".join(ps)+"\n")
    f.write("=== LOG DELTA (%d lines)\n"%len(body)); f.write("\n".join(body)+"\n")
print("wrote %s (%d delta lines)"%(sys.argv[2],len(body)))
PY
rm -f "$SCRIPT" "$RAW"
