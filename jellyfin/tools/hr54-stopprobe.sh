#!/usr/bin/env bash
# hr54-stopprobe.sh - find which command or SHEF key actually stops playURL.
# usage: hr54-stopprobe.sh 'cmd1' 'SHEF:key' ...
set -uo pipefail
D="$(cd "$(dirname "$0")" && pwd)"
UCXML=/opt/middleware_core/system/tv/uconntest
# Point PROBE_URL at any MPEG-TS (H.264 + AC3) you control.
URL="${PROBE_URL:?set PROBE_URL to an http:// MPEG-TS URL, e.g. http://192.0.2.30:8099/long-h264-ac3.ts}"

count() { "$D/hr54.sh" 'grep -c updatePosition /var/viewer/messages.log' 2>/dev/null | grep -aoE '[0-9]+$' | tail -1; }
dtq()  { printf '%s\n' "$1" | "$D/dtq.sh" 2>/dev/null | grep -av '### rc=' | grep -av '^###' | grep -av 'HR54EXP' | head -3; }

printf '<com.directv.druid.dt.DruidTester command="keyPress" fileName="" session="local"/>' >/dev/null
for k in info exit; do
  "$D/hr54.sh" "wget -q -O /dev/null 'http://127.0.0.1:8080/remote/processKey?key=$k&hold=keyPress'" >/dev/null 2>&1
  sleep 1
done

echo "== starting playback"
dtq "<com.ucentric.pvruconnect.DirectTest command=\"playURL\" url=\"$URL\"/>" >/dev/null
sleep 9
a=$(count); sleep 3; b=$(count)
echo "baseline: $a -> $b (advancing=$(( b > a )))"
if [ "${b:-0}" -le "${a:-0}" ]; then echo "playback not running; aborting"; exit 1; fi

for c in "$@"; do
  case "$c" in
    SHEF:*) "$D/hr54.sh" "wget -q -O /dev/null 'http://127.0.0.1:8080/remote/processKey?key=${c#SHEF:}&hold=keyPress'" >/dev/null 2>&1
            rep="key ${c#SHEF:}" ;;
    *)      rep=$(dtq "<com.ucentric.pvruconnect.DirectTest command=\"$c\"/>") ; rep="cmd $c -> ${rep:-<none>}" ;;
  esac
  sleep 4
  p=$(count); sleep 3; q=$(count)
  if [ "${q:-0}" -le "${p:-0}" ]; then
    echo "STOPPED_BY $c   ($rep)"
    exit 0
  fi
  echo "no effect: $c   ($rep)  count=$p->$q"
done
echo "nothing stopped playback"
