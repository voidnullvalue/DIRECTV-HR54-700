#!/bin/sh
# Run on the receiver through hr54.sh if this milestone must be rolled back.
set -eu
BASE=/var/hr54-persist/jellyfin
PLUGIN=/var/network/plugins/7_6933_6840.squashfs
BACKUP=/var/hr54-persist/backup/asset7-before-local-www-and-screensaver.squashfs

rm -f "$BASE/ui/enable-local-www" "$BASE/ui/enable-screensaver-overlay"
/sbin/start-stop-daemon -K -p "$BASE/www.pid" \
    -x "$BASE/bin/hr54-www" 2>/dev/null || true
rm -f "$BASE/www.pid"
/usr/sbin/iptables -D INPUT -i eth0 -s YOUR_LAN_CIDR_PLACEHOLDER \
    -p tcp --dport 8130 -j ACCEPT 2>/dev/null || true
/bin/umount /opt/ui_assets/assetspack/images/screensaver.png 2>/dev/null || true
if [ -f "$BACKUP" ]; then
    cp "$BACKUP" "$PLUGIN.next"
    chmod 644 "$PLUGIN.next"
    sync
    mv "$PLUGIN.next" "$PLUGIN"
fi
sync
echo 'Local frontend stopped, stock screensaver restored live; prior plugin image selected for next boot.'
