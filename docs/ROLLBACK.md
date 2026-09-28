# Rollback

**Establish your rollback path before you make any change.** Every procedure
here assumes you have one.

Two independent levels:

| Level | Restores | Requires |
| --- | --- | --- |
| **A. Asset rollback** | the stock boot/plugin chain | a saved image — no repartitioning |
| **B. Partition rollback** | partition 2 byte-for-byte | a full 15 GiB backup image |

Level A is fast and is what you will actually use. Level B is the nuclear
option. There is also a level 0 — the WiFi NVRAM, which is entirely separate.

---

## Before you start: make your backups

```sh
# 1. Identify the disk by SIZE AND SERIAL, never by /dev/sdX name.
export HR54_DISK_SERIAL=REPLACE_WITH_YOUR_DISK_SERIAL
lsblk -dno NAME,SIZE,SERIAL,MODEL

# 2. Read-only acquisition + hashed evidence bundle.
HR54_DISK=/dev/sdX HR54_EVIDENCE=./evidence tools/acquire_phase1.sh

# 3. Full partition-2 image. 15 GiB. Verify the hash afterwards.
ddrescue /dev/sdX2 ./sdb2.img ./sdb2.map
sha256sum ./sdb2.img
```

The author's reference `sdb2.img` SHA-256 is in
[NOT_INCLUDED.md](NOT_INCLUDED.md). **Yours will differ** — different disk,
different wear, different `/var` history. Use your own.

`ddrescue` rather than `dd`: the original acquisition completed at 100% with
zero errors, and you want the same guarantee.

> If you can only afford one backup, take **partition 2**. It is the only
> partition this project writes. Never `xfs_repair` the original — run it on a
> copy.

---

## Level 0: WiFi NVRAM

Separate from everything else, and the easiest to get wrong.

```sh
# BACK UP FIRST -- this file contains a credential
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'dd if=/dev/nds/nvram0 of=/var/hr54-transfer/nvram0.orig.bin bs=64k'

jellyfin/tools/hr54-pull.sh /var/hr54-transfer/nvram0.orig.bin ./nvram0.orig.bin
chmod 600 ./nvram0.orig.bin
```

Rollback:

```sh
# guarded, verified, self-checking
NVRAM_ACTION=restore LD_PRELOAD=/var/hr54-transfer/nvwrite.so sleep 6
```

`nvwrite.c` refuses to restore if the live image has diverged from both the
original and the patched form. **Keep Ethernet working while you do this** — see
[WIFI.md](WIFI.md#preserve-ethernet-during-setup).

---

## Level A: asset rollback (the usual one)

The hook lives inside the asset-7 plugin image. Put the previous image back.

### A1. Back up the current image before you change it

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh '
  mkdir -p /var/hr54-persist/backup
  cp /var/network/plugins/7/current \
     /var/hr54-persist/backup/asset7-jellyfin-v4-pre-rollback.squashfs
  md5sum /var/hr54-persist/backup/asset7-jellyfin-v4-pre-rollback.squashfs
  sync'
```

Do this **every time** you deploy a new asset image. It is the only thing
standing between you and an unbootable plugin chain.

### A2. Roll back

The atomically-swap-in sequence matters. **Never overwrite `current` in
place** — a power loss mid-write leaves no bootable image.

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh '
  set -e
  PLUG=/var/network/plugins/7
  SRC=/var/hr54-persist/backup/asset7-jellyfin-v3-pre-v4.squashfs
  test -f "$SRC" || { echo "no backup"; exit 1; }
  cp "$SRC" "$PLUG/.next"
  test "$(stat -c %s "$PLUG/.next")" -gt 0 || { echo "empty"; exit 1; }
  md5sum "$PLUG/.next"        # compare against your recorded hash
  sync
  mv "$PLUG/.next" "$PLUG/current"    # atomic rename within the same filesystem
  sync
  echo "SWAPPED_OK"'
```

Then **reboot the receiver** and confirm the middleware comes up and Druid
presents.

Author's reference hashes:

| Image | MD5 |
| --- | --- |
| asset-7 v4 (current production) | `f8cedcc05837d6df1391b4e4f7cfc88a` |
| asset-7 v3 (`asset7-jellyfin-v3-pre-v4.squashfs`) | `cf6ae48990bac41b3ce526ea1681ba85` |
| asset-7 v2 | `09d4626df7d72535eeb2d636ec9a3b0f` |
| asset-7 v3 beachhead (SHA-256) | `1d8738b0…` (see [NOT_INCLUDED.md](NOT_INCLUDED.md)) |

### A3. Caveat

**v3 is an emergency rollback, not supported operation.** v3 launches the
obsolete host-dependent `hr54-www`, which expects a development host on the LAN.
After rolling back to v3 you have a root shell and nothing else — you are back
at the beachhead stage and must re-deploy v4.

### A4. Remove the UI/screensaver overlay

If you also deployed the screensaver bind-mount and the local-www flags:

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'jellyfin/ui/rollback-local-www-and-screensaver.sh'
```

That script drops the flags, kills the `www` process, removes the iptables
rule, **unmounts the bind-mount over
`/opt/ui_assets/assetspack/images/screensaver.png`**, and restores the backup
image. The `umount` line is the important one — without it the stock artwork
stays hidden behind our PNG.

Settings FlatBuffer overlay:

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh 'jellyfin/ui/receiver-rollback'
```

---

## Level B: full partition-2 restore

Only if partition 2 is actually damaged.

```sh
export HR54_DISK_SERIAL=REPLACE_WITH_YOUR_DISK_SERIAL
./asset7/restore-original-sdb2.sh
```

`restore-original-sdb2.sh` is gated on the literal
`RESTORE_ENTIRE_SDB2_FROM_AUTHORITY`, and it asserts disk size, **serial**,
and the SHA-256 of your backup before writing a single block. It refuses to run
if any of those disagree.

> This overwrites **all 15 GiB** of partition 2. Everything in `/var` on
> partition 2 is gone, including recordings metadata. Take a fresh backup of
> anything you care about first.

---

## What is NOT rolled back

**Nothing in flash is ever modified, so nothing in flash needs rolling back.**
CFE, the kernel, and the rootfs in MTD are untouched by this project — that is
the safety property the whole approach depends on. There is no "undo the boot
chain" step because the boot chain was never changed.

Partition 4 (SWDL staging) and the partition table are also never touched.

## Verify after any rollback

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh 'uname -a; df -h /var; ls -la /var/network/plugins/7/'
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54-state.sh
```

Expect: kernel `3.3.8-3.0`, `/var` mounted, `current` present, Druid
presenting a screen.

## Related

- [PERSISTENCE.md](PERSISTENCE.md) — what the hook does
- [ROOT.md](ROOT.md) — the install discipline
- [WIFI.md](WIFI.md) — NVRAM specifically
- [NOT_INCLUDED.md](NOT_INCLUDED.md) — how to obtain the restore image
