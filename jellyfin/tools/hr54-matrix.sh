#!/usr/bin/env bash
# hr54-matrix.sh <outdir> <url> [url ...]
# Dismiss the screensaver, then run playURL against each URL in sequence,
# writing one log slice per URL.
set -uo pipefail
HOST="${HR54_HOST:-192.0.2.10}"   # 192.0.2.0/24 = RFC 5737 TEST-NET-1, never a real host
[ -n "${HR54_HOST:-}" ] || { echo "set HR54_HOST to the receiver's LAN address" >&2; exit 2; }; PORT="${HR54_PORT:-5777}"
OUTDIR="$1"; shift
mkdir -p "$OUTDIR"
SCRIPT=$(mktemp); RAW=$(mktemp)
# matrix file first
{
  printf 'cat > /var/hr54-transfer/matrix.txt <<%s\n' "'MTXEOF'"
  i=0
  for u in "$@"; do
    printf 'case%02d|%s|22\n' "$i" "$u"
    i=$((i+1))
  done
  printf 'MTXEOF\n'
  cat <<'OUTER'
cat > /var/hr54-transfer/matrix-run.sh <<'MRUN'
UC=/opt/middleware_core/system/tv/uconntest
LOG=/var/viewer/messages.log
echo "@@WAKE@@"
for k in info exit; do
  wget -q -O /dev/null "http://127.0.0.1:8080/remote/processKey?key=$k&hold=keyPress"
  sleep 1
done
sleep 2
while IFS='|' read -r tag url wait; do
  [ -z "$tag" ] && continue
  A="$$EXP-$tag"
  B="$$EXPEND-$tag"
  echo "$A $(date +%H:%M:%S)" >> $LOG
  echo "@@CASE@@$tag $url"
  $UC "<com.ucentric.pvruconnect.DirectTest command=\"playURL\" url=\"$url\"/>" 2>&1
  echo "RC=$?"
  sleep "$wait"
  echo "$B $(date +%H:%M:%S)" >> $LOG
  sleep 1
  echo "@@CASEDELTA@@$tag"
  sed -n "/$A/,/$B/p" $LOG
done < /var/hr54-transfer/matrix.txt
echo "@@PS@@"
ps
echo "@@END@@"
MRUN
sh /var/hr54-transfer/matrix-run.sh
rm -f /var/hr54-transfer/matrix.txt /var/hr54-transfer/matrix-run.sh
exit
OUTER
  sleep 5
} | timeout 3000 nc -q 3 "$HOST" "$PORT" > "$RAW" 2>/dev/null
python3 - "$RAW" "$OUTDIR" <<'PY'
import sys, os
raw=open(sys.argv[1],encoding='latin1').read()
lines=raw.splitlines()
outdir=sys.argv[2]
cur=None; buf=[]
def flush():
    global cur,buf
    if cur:
        with open(os.path.join(outdir,'%s.log'%cur),'w',encoding='latin1') as f:
            f.write("\n".join(buf)+"\n")
        print("wrote %s.log (%d lines)"%(cur,len(buf)))
    cur=None; buf=[]
for l in lines:
    s=l.strip()
    if s.startswith('@@CASE@@'):
        flush()
        cur=s[len('@@CASE@@'):].split()[0]
        buf=[l]
    elif s.startswith('@@'):
        flush(); continue
    else:
        if cur is not None: buf.append(l)
flush()
PY
rm -f "$SCRIPT" "$RAW"
