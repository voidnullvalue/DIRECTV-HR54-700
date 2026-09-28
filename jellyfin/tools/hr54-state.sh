#!/usr/bin/env bash
# hr54-state.sh - capture Druid/OSD/player/process state from the HR54
set -uo pipefail
HOST="${HR54_HOST:-192.0.2.10}"   # 192.0.2.0/24 = RFC 5737 TEST-NET-1, never a real host
[ -n "${HR54_HOST:-}" ] || { echo "set HR54_HOST to the receiver's LAN address" >&2; exit 2; }; PORT="${HR54_PORT:-5777}"
{
  cat <<'OUTER'
UC=/opt/middleware_core/system/tv/uconntest
DT=/opt/dtv/dt
echo "===== date"; date
echo "===== processes"
ps | grep -E "dtvwm|Siege|dvr_core|mediaplayer|media|hls|mp4|dcdp|nexus|uconn" | grep -v grep
echo "===== druid screen"
$UC '<com.directv.druid.dt.DruidTester command="getCurrentScreenId" session="local"/>' 2>&1
echo "===== OSD registry"
for d in /var/mw_registry/Registry/Device/Server/OSD/Current; do
  for f in Number Extension; do
    printf '%s: ' "$f"
    cat $d/$f 2>/dev/null || echo "(none)"
    echo
  done
done
echo "===== mediaplayer state"
$UC '<com.directv.druid.dt.DruidTester command="listLocalUI" session="local"/>' 2>&1 | head -40
echo "===== log sizes"
ls -la /var/viewer/messages.log /var/viewer/netevt.log
exit
OUTER
  sleep 6
} | timeout 240 nc -q 3 "$HOST" "$PORT" 2>/dev/null
