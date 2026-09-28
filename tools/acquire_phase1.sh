#!/bin/bash
# Phase-1 forensic acquisition: read-only identification of the attached disk.
# Nothing here writes to the disk. Output is a hashed evidence bundle.
#
#   HR54_DISK=/dev/sdb HR54_EVIDENCE=./evidence ./acquire_phase1.sh
set -u
: "${HR54_DISK:?set HR54_DISK to the disk device, e.g. /dev/sdb}"
base="${HR54_EVIDENCE:-./evidence}"
log="$base/logs"
mkdir -p "$log"
lsblk -b -o NAME,PATH,MODEL,SERIAL,SIZE,RO,TYPE,FSTYPE,FSVER,LABEL,UUID,PARTTYPE,PARTLABEL,MOUNTPOINTS > "$log/lsblk.txt" 2>&1
fdisk -l "$HR54_DISK" > "$log/fdisk.txt" 2>&1
parted -s "$HR54_DISK" unit s print > "$log/parted-sectors.txt" 2>&1
blkid -p $HR54_DISK "$HR54_DISK"1 "$HR54_DISK"2 "$HR54_DISK"3 "$HR54_DISK"4 > "$log/blkid.txt" 2>&1
wipefs -n $HR54_DISK "$HR54_DISK"1 "$HR54_DISK"2 "$HR54_DISK"3 "$HR54_DISK"4 > "$log/wipefs.txt" 2>&1
file -s $HR54_DISK "$HR54_DISK"1 "$HR54_DISK"2 "$HR54_DISK"3 "$HR54_DISK"4 > "$log/file-source.txt" 2>&1
sgdisk -p $HR54_DISK > "$log/sgdisk.txt" 2>&1
smartctl -x $HR54_DISK > "$log/smart-default.txt" 2>&1
smartctl -x -d sat $HR54_DISK > "$log/smart-sat.txt" 2>&1
smartctl -x -d scsi $HR54_DISK > "$log/smart-scsi.txt" 2>&1
hdparm -I $HR54_DISK > "$log/hdparm.txt" 2>&1
blockdev --getsize64 $HR54_DISK > "$log/blockdev-size.txt" 2>&1
blockdev --getro $HR54_DISK > "$log/blockdev-ro.txt" 2>&1
date -Ins > "$log/acquisition-phase1-complete.txt"
sha256sum "$log"/*.txt > "$log/SHA256SUMS.phase1"
