#!/usr/bin/env bash
# hr54.sh - run a command on the HR54 root shell (TCP/5777)
# usage: hr54.sh 'uname -a'   or   hr54.sh -f script.sh
set -uo pipefail
HOST="${HR54_HOST:-192.0.2.10}"   # 192.0.2.0/24 = RFC 5737 TEST-NET-1, never a real host
[ -n "${HR54_HOST:-}" ] || { echo "set HR54_HOST to the receiver's LAN address" >&2; exit 2; }
PORT="${HR54_PORT:-5777}"
SCRIPT=$(mktemp)
if [ "${1:-}" = "-f" ]; then cat "${2:?usage: hr54.sh -f script.sh}" > "$SCRIPT"; else printf '%s\n' "${1:-}" > "$SCRIPT"; fi
out=$( { cat "$SCRIPT"; printf '\nexit\n'; sleep 1; } | timeout "${HR54_TIMEOUT:-60}" nc -q 2 "$HOST" "$PORT" 2>&1 )
rm -f "$SCRIPT"
printf '%s\n' "$out"
