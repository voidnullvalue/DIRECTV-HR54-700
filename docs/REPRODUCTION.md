# Reproduction

End-to-end order of operations. Each stage links to the exact tool.

> ### ⚠ Before stage 1, please
>
> **Make a full read-only backup of the disk.** Every stage after this one writes
> to it. [ROLLBACK.md](ROLLBACK.md#before-you-start-make-your-backups) is not
> optional reading.
>
> And know that TCP/5777 is an **unauthenticated root shell**.
> [ROOT.md](ROOT.md#root-shell) — trusted private LAN only.

Set these once:

```sh
export HR54_HOST=<your-hr54-ip>              # the receiver
export HR54_DISK_SERIAL=<your-disk-serial>   # lsblk -dno NAME,SIZE,SERIAL
export HR54_DISK=/dev/sdX
export JELLYFIN_SERVER=http://<your-jellyfin>:8096
```

---

## 1. Identify the hardware

Confirm you have an HR54-700 and that the platform is what this project expects.

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'uname -a; cat /proc/cpuinfo | head -5; df -h'
```

Expect: **MIPS**, Linux **3.3.8-3.0**, uClibc, ~1 GB RAM, a ~1 TB disk.
Full platform detail: [HARDWARE.md](HARDWARE.md).

**Also confirm you own the receiver and it is yours to modify.** This project
permanently alters a commercial device's software. That is fine for your own
hardware and not fine for someone else's.

---

## 2. Back up disk and filesystem

```sh
# read-only identification + hashed evidence
HR54_DISK=/dev/sdX HR54_EVIDENCE=./evidence tools/acquire_phase1.sh

# full partition-2 image (the only partition this project writes)
ddrescue /dev/sdX2 ./sdb2.img ./sdb2.map
sha256sum ./sdb2.img
```

**Record that hash now.** [ROLLBACK.md](ROLLBACK.md). If you can afford one
backup, this is it. Never `xfs_repair` the original — copy first.

---

## 3. Obtain persistent root

Read [ROOT.md](ROOT.md) **in full before running anything** — especially the
section on signature non-forgery, so you understand what you are doing.

Obtain the genuine inputs from your own receiver
([PROPRIETARY_INPUTS.md](PROPRIETARY_INPUTS.md)):

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'cp /var/network/plugins/7/7_6932_6932.squashfs /var/hr54-transfer/
   cp /var/network/plugins/7/7_6932_6932.sig     /var/hr54-transfer/'

jellyfin/tools/hr54-pull.sh /var/hr54-transfer/7_6932_6932.squashfs ./
jellyfin/tools/hr54-pull.sh /var/hr54-transfer/7_6932_6932.sig ./
sha256sum 7_6932_6932.squashfs 7_6932_6932.sig
```

**Offline verification gate — do not skip this:**

```sh
./asset7/verify-offline-v3.sh
```

It must print `OFFLINE_VERIFY_V3=PASS`. It checks `sh -n` on every injected
script, mode `0700`, that the launcher runs and writes its marker, and
**separately that the preserved `indexer.real` still returns 22 and still
prints its banner** — i.e. vendor behaviour is unharmed.

Then install:

```sh
HR54_DISK_SERIAL=<your-disk-serial> \
  ./asset7/install-v3-via-mips-vm.sh I_UNDERSTAND_ONLY_CONFIRMED_PARTITION_2
```

The MIPS-QEMU guest variant is the architecturally honest one: only `sdb2` is
visible to the guest, so a bug cannot reach another disk. (`install-to-sdb2.sh`
is the direct host-side equivalent.)

**Reboot.** Then confirm:

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh 'id; cat /var/HR54_ROOT_PROOF'
```

`id` → `uid=0(root)`. `HR54_ROOT_PROOF` → `HR54_ROOT_EXEC`. You have root that
survives a power cycle.

---

## 4. Install the persistent `/var` boot hook

The hook is the asset-7 `indexer` wrapper. It runs on every boot and is what
everything in stages 5+ depends on.

```sh
# back up the current image FIRST — this is your Level A rollback
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh '
  mkdir -p /var/hr54-persist/backup
  cp /var/network/plugins/7/current /var/hr54-persist/backup/asset7-pre-jellyfin.squashfs
  md5sum /var/hr54-persist/backup/asset7-pre-jellyfin.squashfs; sync'
```

Rebuild the asset-7 image with the boot hook, using the
`jellyfin/ui/activate-local-www-and-screensaver.sh` flow, and stage it via TFTP
([PROPRIETARY_INPUTS.md](PROPRIETARY_INPUTS.md)). Create the durable tree first:

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh '
  mkdir -p /var/hr54-persist/jellyfin/{bin,www/tv,config,state,cache,log}
  chmod 700 /var/hr54-persist /var/hr54-persist/jellyfin /var/hr54-persist/jellyfin/config
  sync'
```

Details: [PERSISTENCE.md](PERSISTENCE.md).

---

## 5. Build and deploy the receiver-native backend

**The most important artifact in this repository.**

```sh
# -mcpu=mips32 is REQUIRED. mips32r2 gives Illegal Instruction on hardware.
zig cc -target mips-linux-musleabi -mcpu=mips32 \
  -static -O2 -o jellyfin/remote/bin/hr54-jf jellyfin/remote/hr54_jf.c
```

Verify before deploying:

```sh
jellyfin/remote/test/run_host_tests.sh     # 37 assertions, must print ALL HOST TESTS PASSED
```

Deploy:

```sh
jellyfin/tools/hr54-tftp-put.py jellyfin/remote/bin/hr54-jf /var/hr54-persist/jellyfin/bin/ on-receiver
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'chmod 755 /var/hr54-persist/jellyfin/bin/hr54-jf; md5sum /var/hr54-persist/jellyfin/bin/hr54-jf'
```

Production path: **`/var/hr54-persist/jellyfin/bin/hr54-jf`**
Production port: **`8130`**

Run it:

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh '
  /var/hr54-persist/jellyfin/bin/hr54-jf \
    /var/hr54-persist/jellyfin/www '"$JELLYFIN_SERVER_HOST"' 8096 8130 >/var/hr54-persist/jellyfin/log/jf.log 2>&1 &'
```

> The appliance talks **directly to the Jellyfin server**. It does **not** need
> the development host. Verified with that host powered off.
> [JELLYFIN.md](JELLYFIN.md)

---

## 6. Stage the TV frontend

ES5, no build step, no npm. Copy the three files:

```sh
for f in index.html app.js app.css; do
  jellyfin/tools/hr54-tftp-put.py jellyfin/remote/static/tv/$f /var/hr54-persist/jellyfin/www/tv/ on-receiver
done
```

`[ITV-WEBKIT.md](ITV-WEBKIT.md)`. The user triggers it by pressing **MENU** and
leaving it idle briefly; a watcher clears the stock presentation, walks Druid to
LiveTV, and starts the renderer fullscreen.

---

## 7. Configure the Jellyfin server

Two things:

1. **Network** — reachable from the receiver. No port forwarding, ever.
2. **Device profile** — advertise MPEG-TS / H.264 ≤1080p / AC3 48 kHz stereo /
   LAN HTTP, and **do not offer HLS or DASH**. The server must hand back a
   direct MPEG-TS stream or the receiver cannot play it.
   [PLAYBACK.md](PLAYBACK.md#working-codec-profile)

Create a non-admin user for the receiver. Quick Connect works best with one.

---

## 8. Quick Connect

Press **MENU** on the TV. The UI shows a **six-character code**. Approve it in
the Jellyfin app or web UI. The TV polls and signs in.

The `Secret` and the resulting `AccessToken` **never leave the backend**. The
token is persisted to `config/token` at mode `0600` and survives reboot.

```sh
# confirm auth state (from a host that can reach the receiver)
curl -s http://<your-hr54-ip>:8130/api/auth/status
```

Flow detail: [JELLYFIN.md](JELLYFIN.md#authentication-quick-connect).

---

## 9. Configure WiFi (optional — only if you want to cut the cable)

**Keep Ethernet working throughout.** [WIFI.md](WIFI.md#preserve-ethernet-during-setup).

```sh
# 1. back up NVRAM (contains a credential -- chmod 600, never commit)
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'dd if=/dev/nds/nvram0 of=/var/hr54-transfer/nvram0.orig.bin bs=64k'
jellyfin/tools/hr54-pull.sh /var/hr54-transfer/nvram0.orig.bin ./nvram0.orig.bin
chmod 600 ./nvram0.orig.bin

# 2. pull the vendor NVRAM library and build the tools
jellyfin/tools/hr54-pull.sh /opt/nvram/lib/libDtvNVRamMgr.so ./
ZIG=/path/to/zig LIB=$(pwd) ./wifi/build.sh

# 3. patch YOUR copy -- credentials from you, never from the repo
python3 wifi/patch-nvram.py nvram0.orig.bin nvram0.mine.bin "$YOUR_SSID" "$YOUR_PASSPHRASE"
#    ...or: printf '%s\n%s\n' "$SSID" "$PASS" | python3 wifi/patch-nvram.py ...
#    ...or: --config ~/.config/hr54-wifi.conf   (mode 600, gitignored)

# 4. verify the byte diff and checksum before it goes near the device
#    (script in WIFI.md "Verifying")

# 5. guarded, reversible write
ZIG=/path/to/zig LIB=$(pwd) ./wifi/build-write.sh
#    on the receiver:
#    NVRAM_ACTION=check   ... then NVRAM_ACTION=apply
#    revert any time with  NVRAM_ACTION=restore
```

`patch-nvram.py` refuses to patch if the image does not match the expected
layout — if it refuses, **stop**. Your firmware differs; re-derive the offsets
with `wifi/nvfields.c`.

---

## 10. Enable the menu/UI launcher

If you want a launcher entry rather than the MENU press:

```sh
# inspect the stock Settings FlatBuffer (read-only, from-scratch parser)
python3 jellyfin/ui/inspect_settings.py /path/to/STB_Settings.fb

# build overlay candidates
python3 jellyfin/ui/add_settings_row.py /path/to/STB_Settings.fb jellyfin.fb
python3 jellyfin/ui/rename_help_tile.py  /path/to/STB_Settings.fb jellyfin-tv.fb

# screensaver: our own artwork, bind-mounted over the stock slot
jellyfin/tools/hr54-tftp-put.py assets/screensaver-hax0r.png /var/hr54-transfer/ on-receiver
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'jellyfin/ui/activate-local-www-and-screensaver.sh'
```

**Know the limitation before you build these:** the compiled
`createMenuRowMap` whitelists seven stock sections, so an arbitrary new row will
**not** render. The overlay mechanism itself works. See
[DEAD-ENDS.md](DEAD-ENDS.md#4-a-new-arbitrary-settings-row-is-whitelisted-out).

Rollback: `jellyfin/ui/rollback-local-www-and-screensaver.sh` and
`jellyfin/ui/receiver-rollback`. **The `umount` line is load-bearing.**

---

## 11. Verify playback

Pick a title that matches the device profile. Confirm on the **physical
screen** — there is no capture path.

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh 'jellyfin/tools/receiver-hr54-play-url'
```

You should see 1080p H.264 + AC3 via the Broadcom hardware decoder, with flat
CPU load. If it does not appear, work through
[DEAD-ENDS.md](DEAD-ENDS.md) — the iTV/Druid presentation problem and the
`mips32r2` problem both look like "nothing happens".

---

## 12. Verify transport

On the remote, with playback running:

| Key | Expected |
| --- | --- |
| Pause | picture **freezes**, player stays up, connection stays open |
| Pause again | resumes seamlessly |
| Wait > threshold, then resume | stream restarts at the saved time, playback continues |
| Left / Right | seeks, playback continues |
| Stop | returns to the UI |
| MENU | exits playback, restores your browse position |

The distinguishing test for pause: **the socket must stay open.** If closing it
is how you implemented pause, the receiver will drop to the menu instead.
[PLAYBACK.md](PLAYBACK.md#transport-pause-resume-seek-stop)

Assert the same behaviours headlessly any time you change the backend:

```sh
jellyfin/remote/test/run_host_tests.sh
```

---

## 13. Verify reboot persistence

```sh
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh 'reboot'
# wait for the receiver to come back
```

Then confirm all of:

- [ ] `id` → `uid=0(root)` over TCP/5777
- [ ] `/var/HR54_ROOT_PROOF` still present
- [ ] `/var/hr54-persist/jellyfin/bin/hr54-jf` present and running
- [ ] `curl http://<hr54>:8130/api/status` responds
- [ ] `curl http://<hr54>:8130/api/auth/status` → **already authenticated** (token survived)
- [ ] MENU launches the fullscreen UI
- [ ] playback works
- [ ] browse position was restored

**Only the boot hook re-runs.** Nothing is being re-deployed; if something
needed re-copying, your `/var/hr54-persist` layout is wrong.
[PERSISTENCE.md](PERSISTENCE.md)

---

## Where things are

| Need | Read |
| --- | --- |
| What the system is | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Platform, storage, boot chain | [HARDWARE.md](HARDWARE.md) |
| The exploit, and why we don't forge signatures | [ROOT.md](ROOT.md) |
| Surviving reboot | [PERSISTENCE.md](PERSISTENCE.md) |
| Media path and transport control | [PLAYBACK.md](PLAYBACK.md) |
| The appliance in detail | [JELLYFIN.md](JELLYFIN.md) |
| The TV UI | [ITV-WEBKIT.md](ITV-WEBKIT.md) |
| WiFi / NVRAM | [WIFI.md](WIFI.md) |
| Undoing it | [ROLLBACK.md](ROLLBACK.md) |
| What to obtain yourself | [PROPRIETARY_INPUTS.md](PROPRIETARY_INPUTS.md) |
| What is excluded, and why | [NOT_INCLUDED.md](NOT_INCLUDED.md) |
| **What not to waste time on** | [DEAD-ENDS.md](DEAD-ENDS.md) |
| Full security analysis | [ORIGINAL-REPORT.md](ORIGINAL-REPORT.md) |
