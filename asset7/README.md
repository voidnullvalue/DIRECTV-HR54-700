# HR54 asset-7 hardware proof and beachhead

Current status and exact hashes are in `STATE.md`. Never assume the disk is
`/dev/sdb`; resolve it by the recorded 1 TB size, serial, and all partition
geometry.

The current v3 package replaces two files, and only two files, on confirmed
physical partition 2:

- `/var/network/plugins/7_6933_6840.squashfs`
- `/var/network/plugins/7_6933_6840.sig`

It never writes `/dev/sdb1`, `/dev/sdb3`, `/dev/sdb4`, the partition table, or
raw sectors. The `.sig` authenticates the already-present genuine
`7_6932_6932.squashfs`; the root plugin installer mounts the different,
higher-version image.

V1 proved signature acceptance and malicious mount on hardware but used the
wrong native trigger. `Mp4libFeatureStarter` executes the indexer during its
availability probe; it does not load `libmp4lib.so`. V2 attached the proof to
that deterministic path and created exact `/var/HR54_ROOT_PROOF` on the real
receiver. V3 wraps the same entry point with a LAN-only root shell, stock TFTP
service, and persistent hardware inventory while preserving the genuine
library and original vendor indexer.

## Build and offline gate (v3)

```sh
python3 ../linux-port/beachhead/build-hr54d.py
./build-plugin-v3.sh
./verify-offline-v3.sh
```

Do not install unless the final line is `OFFLINE_VERIFY_V3=PASS`.

## Install

With the powered-down HR54 disk attached. Identify the disk by size **and**
serial first, and read the offline-verification notes in
[../docs/ROOT.md](../docs/ROOT.md) before running anything.

```sh
lsblk -dno NAME,SIZE,SERIAL,MODEL        # find YOUR disk
export HR54_DISK_SERIAL=<your-disk-serial>
sudo -v                                  # or set HR54_SUDO_PASS / ~/.hr54-sudo-pass
./install-v3-via-mips-vm.sh I_UNDERSTAND_ONLY_CONFIRMED_PARTITION_2
```

`uninstall-files.sh` deliberately does nothing: it prints the two paths to
remove and exits non-zero. The refusal is the point.

The script dynamically resolves the unique disk with serial `<DISK_SERIAL>`,
validates the disk plus all four partition sizes and starts, requires read-only
entry, and runs the offline gate. Only confirmed partition 2 is exposed to the
big-endian MIPS guest. The v3 installer additionally refuses the write unless
the current v2 image and hardware-created proof hashes match. A separate
read-only guest verifies v3, its signature, the genuine anchor, and the proof
before returning with the whole disk read-only.

## Hardware test

This sequence completed successfully on 2026-09-27. The live receiver was
`the receiver`; TCP/5777 returned a root shell, UDP/1069 transferred the
complete inventory, and the live result is recorded in `STATE.md`.

1. Connect the HR54 Ethernet port to a trusted private LAN, reinstall the
   already-prepared disk, and boot normally.
2. Let it reach the GUI/dish-error state and remain powered for five additional
   minutes. Record any boot loop, crash, or visible anomaly.
3. Determine its DHCP address from the router and connect to TCP/5777 from a
   machine on the same private LAN. The service is an unauthenticated root
   shell, intentionally limited to private/link-local sources by iptables.
4. Leave the receiver running if the shell opens. If it does not, shut down,
   return the disk without mounting it, and inspect `/var/hr54-beachhead` and
   `/var/hr54-inventory` through the big-endian helper.

On any boot failure, do not experiment on the live filesystem. Run
`restore-original-sdb2.sh RESTORE_ENTIRE_SDB2_FROM_AUTHORITY`, verify the
recorded XFS metadata, and boot again. The whole-partition restore is the
authoritative rollback.
