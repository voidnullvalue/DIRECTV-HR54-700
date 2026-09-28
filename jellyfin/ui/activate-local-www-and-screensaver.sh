#!/bin/sh
set -eu
BASE=/var/hr54-persist/jellyfin
PLUGIN=/var/network/plugins/7_6933_6840.squashfs
mkdir -p "$BASE/ui" "$BASE/log" /var/hr54-persist/backup
cp "$PLUGIN" /var/hr54-persist/backup/asset7-before-local-www-and-screensaver.squashfs
cp /var/hr54-transfer/screensaver-hax0r.png "$BASE/ui/screensaver-hax0r.png"
chmod 644 "$BASE/ui/screensaver-hax0r.png"
touch "$BASE/ui/enable-screensaver-overlay" "$BASE/ui/enable-local-www"
cp /var/hr54-transfer/asset7-jellyfin-v3.squashfs "$PLUGIN.next"
chmod 644 "$PLUGIN.next"
sync
mv "$PLUGIN.next" "$PLUGIN"
sync
md5sum "$PLUGIN" "$BASE/ui/screensaver-hax0r.png" /var/hr54-persist/backup/asset7-before-local-www-and-screensaver.squashfs
