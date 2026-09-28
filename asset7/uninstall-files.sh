#!/bin/sh
set -eu
echo "File-level uninstall is intentionally not automatic while the disk is attached." >&2
echo "Mount only /dev/sdb2 with the same guards as install-to-sdb2.sh, then remove exactly:" >&2
echo "  /var/network/plugins/7_6933_6840.squashfs" >&2
echo "  /var/network/plugins/7_6933_6840.sig" >&2
echo "The authoritative rollback is restore-original-sdb2.sh." >&2
exit 1
