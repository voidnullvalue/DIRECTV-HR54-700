# DIRECTV HR54-700 → standalone Jellyfin box

I took a DIRECTV HR54-700 Genie DVR, got persistent root on it, found the stock
Broadcom playback path, made the vendor ITV/WebKit stack render a usable
Jellyfin UI, moved the entire backend onto the receiver, and ended up with a
standalone Jellyfin set-top box that talks directly to my Jellyfin server over
Wi-Fi.

## 📖 The long-form writeup

**https://voidnullvalue.github.io/DIRECTV-HR54-700/**

The page intentionally follows the same terminal-style look and tone as my
Google Glass / `glassterm` writeup. It is preserved here in `index.html` and is
the narrative account of the work.

**This repository is the reproducible implementation.** The article is the
story; this is the source, the tooling, and the documentation needed to rebuild
it on your own receiver.

---

## What this is

A complete, from-source implementation of a self-hosted Jellyfin client running
**on the receiver itself**, plus the reverse-engineering notes and tooling
needed to reproduce the persistent-root mechanism on your own HR54-700.

```text
DIRECTV remote
      │
      ▼
HR54 ITV/WebKit UI          (ES5, fullscreen, our own)
      │
      ▼
receiver-native hr54-jf :8130   (static MIPS32 C, ~87 KB, no deps)
      │
      ▼
Jellyfin server :8096
      │
      ▼
MPEG-TS / H.264 / AC3
      │
      ▼
Broadcom CDI/NEXUS hardware decoder ──► HDMI
```

**No helper PC is required at runtime.** Verified with the development host
powered off.

## Current result

- Persistent root that survives a power cycle
- Native C backend, self-contained, one process
- TV browser UI: browse, search, on-screen keyboard, paging, artwork, saved
  browse state, return-from-playback
- Quick Connect sign-in from the TV, token persisted `0600` across reboot
- 1080p H.264 + AC3 playback through the hardware decoder
- **Working transport: pause, resume, long-pause restart, seek, stop** — all
  physically confirmed
- Full rollback at two levels

## Hardware

Broadcom **BCM7346B2**, dual-core **BMIPS5000** at 1305 MHz, **MIPS32
big-endian**, 1 GB RAM, 1 TB disk, Linux 3.3.8-3.0, uClibc.
Full detail: [docs/HARDWARE.md](docs/HARDWARE.md).

## Architecture in one paragraph

The HR54 boots its trusted OS from **flash**, but the plugin that runs
arbitrary vendor code comes from the **writable disk**. A filename/path
confusion in the plugin installer means the signature check verifies one file
while the loader mounts another — so a substituted asset image boots. The
middleware's MP4 feature probe then *executes* `/opt/mp4lib/bin/indexer`, which
is now a shell wrapper. That is the beachhead, and a boot hook built from it
makes everything persistent.

> ### The one thing to understand first
>
> **No signature is forged.** The genuine vendor signature is reused verbatim
> and is never modified. There is no cryptographic break here — it is a
> path/filename confusion. Read [docs/ROOT.md](docs/ROOT.md) before anything
> else.

## Prerequisites

- An **HR54-700 you own** and are willing to modify
- A host machine, a **read-only** disk workflow, and `ddrescue`
- A Jellyfin server reachable from the receiver
- `zig` (or any `mips-linux-musleabi` toolchain) to build the native backend
- `curl` and Python 3 to run the tests
- Patience for stage 1: making a full backup

## Reproduce it

Thirteen stages, each linking to the exact tool:

**[→ docs/REPRODUCTION.md](docs/REPRODUCTION.md)**

1. Identify the hardware
2. Back up disk and filesystem
3. Obtain persistent root
4. Install the persistent `/var` boot hook
5. Build and deploy the receiver-native backend
6. Stage the TV frontend
7. Configure the Jellyfin server
8. Quick Connect
9. Configure WiFi (optional)
10. Enable the menu/UI launcher
11. Verify playback
12. Verify transport
13. Verify reboot persistence

Build the backend and run the test suite:

```sh
# -mcpu=mips32 is REQUIRED; mips32r2 gives Illegal Instruction on hardware.
zig cc -target mips-linux-musleabi -mcpu=mips32 \
  -static -O2 -o jellyfin/remote/bin/hr54-jf jellyfin/remote/hr54_jf.c

jellyfin/remote/test/run_host_tests.sh     # 37 assertions
```

## ⚠ Safety, backups, and an unauthenticated root shell

- **Back up partition 2 (15 GiB) before changing anything.** It is the only
  partition this project writes. → [docs/ROLLBACK.md](docs/ROLLBACK.md)
- **TCP/5777 is an unauthenticated root shell.** Any host on your LAN that can
  reach it gets root. The wrapper installs a `restrict_port` rule allowing
  `127/8`, `10/8`, `172.16/12`, `192.168/16`, `169.254/16` — that is the *only*
  thing protecting you. **Never port-forward 5777.** Tunnel it if you must.
- **Nothing in flash is ever written.** MTD, CFE, the kernel, and partition 4
  are untouched. That is the safety property the whole approach rests on.
- **Keep Ethernet working while changing WiFi.** If the NVRAM write leaves the
  box unreachable, recovery is physical.
- This work was done on hardware the author physically owned, and is published
  to document a vulnerability so owners can understand it. It performs **no
  paid-content decryption** and **no access-control bypass** — the DVR
  entitlement gate was found closed and abandoned, not circumvented.

## Proprietary-input policy

**No DIRECTV firmware, disk image, vendor binary, or copyrighted content is
redistributed in this repository.** The genuine signed plugin image, `sigtst`,
`libDtvNVRamMgr.so`, `dtv.car`, the stock UI resources, and the genuine
`indexer` are all obtained by you **from your own receiver**.

This is not only a legal necessity — the exploit *depends* on the real content,
so you need those files anyway.

**[→ docs/PROPRIETARY_INPUTS.md](docs/PROPRIETARY_INPUTS.md)** — every input,
where it lives on your device, how to copy it, what we do to it, and the output
name.

**[→ docs/NOT_INCLUDED.md](docs/NOT_INCLUDED.md)** — the complete exclusion
list: every file left out, its exact original path, why, and how to regenerate
it. Includes all redactions (a hardcoded sudo password, a recovered root
password hash, a WiFi SSID, the receiver's LAN address, MAC, disk serial, and
DIRECTV card/receiver IDs).

## Documentation

| Document | Read it for |
| --- | --- |
| [REPRODUCTION.md](docs/REPRODUCTION.md) | **Start here.** The 13-stage order of operations |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the pieces fit together |
| [HARDWARE.md](docs/HARDWARE.md) | Platform, disk layout, boot chain, the `mips32` trap |
| [ROOT.md](docs/ROOT.md) | The vulnerability, and why we forge nothing |
| [PERSISTENCE.md](docs/PERSISTENCE.md) | Surviving reboot; the `/var/hr54-persist` layout |
| [PLAYBACK.md](docs/PLAYBACK.md) | The proven media path and transport control |
| [JELLYFIN.md](docs/JELLYFIN.md) | The appliance: backend, auth, frontend, tests |
| [ITV-WEBKIT.md](docs/ITV-WEBKIT.md) | How the TV UI is hosted; the Druid dead end |
| [WIFI.md](docs/WIFI.md) | NVRAM reverse engineering and WiFi provisioning |
| [ROLLBACK.md](docs/ROLLBACK.md) | Undoing all of it |
| [DEAD-ENDS.md](docs/DEAD-ENDS.md) | **What not to waste time on** |
| [ORIGINAL-REPORT.md](docs/ORIGINAL-REPORT.md) | The full security analysis (redacted) |
| [PROPRIETARY_INPUTS.md](docs/PROPRIETARY_INPUTS.md) | What to obtain from your own receiver |
| [NOT_INCLUDED.md](docs/NOT_INCLUDED.md) | What is excluded, and how to regenerate it |
| [SETTINGS-FLATBUFFER.md](docs/SETTINGS-FLATBUFFER.md) | Settings resource RE and the whitelist dead end |

## Repository layout

| Path | Contents |
| --- | --- |
| `jellyfin/remote/` | Native C backend (`hr54_jf.c`), TV frontend, **37-assertion test suite** |
| `jellyfin/client/` | Python Quick Connect + auth reference |
| `jellyfin/tools/` | Receiver shell, TFTP transfer, playback probes, log slicing |
| `jellyfin/ui/` | Settings FlatBuffer tools, screensaver, menu launcher |
| `jellyfin/re/` | Raw reverse-engineering notes (marked `.note.md`) |
| `wifi/` | NVRAM RE, read-only probes, host patcher, guarded on-box writer |
| `asset7/` | Plugin build, offline verification, install, rollback |
| `linux-port/beachhead/` | `hr54d` root-shell daemon, `indexer` wrapper, inventory |
| `userland-mods/` | DVR/subscription-gate research (mostly negative results) |
| `tools/` | Forensics, crypto, and plugin-signature analysis |
| `pvr/` | Recording metadata format + cataloguer (synthetic sample) |
| `assets/` | `screensaver-hax0r.png` — our own artwork |
| `docs/` | All documentation |

## Dead ends, documented

These cost real time and **did not work**. Each is written up in
[docs/DEAD-ENDS.md](docs/DEAD-ENDS.md) with what was tried, why it fails, and
what to do instead:

- `kexec` → `ENOSYS` (not implemented; not a permissions problem)
- DVR entitlement / OSD 117 — closed through three independent entry points
- **`playURL` creates no `VODCapture`**, so native pause/stop return *success*
  while doing nothing
- A new arbitrary Settings row is whitelisted out by a hardcoded seven-entry map
- DirectTest ITV start vs Druid presentation ownership — a genuine deadlock
- Screensaver: the obvious resource path is a symlink and the mount silently
  does nothing
- **`-mcpu=mips32r2` passes every local test, then dies with Illegal
  Instruction on hardware**
- The `.25` host backend prototype (port 8131) — superseded and retired
- Dirty COW is genuinely present, and genuinely unnecessary

## Repository status

- Working and verified on the author's hardware: root, UI, sign-in, playback,
  transport, reboot persistence
- 37 host tests passing; all Python compiles; all shell parses under `sh` and
  `bash`; all C syntax-checks clean
- Proprietary inputs documented but deliberately not redistributed
- No licence grant has been made yet — see [LICENSING.md](LICENSING.md)
- Contributions and corrections welcome; please open an issue rather than
  assuming a licence

## Licence and trademarks

**No `LICENSE` file, deliberately.** The original source here is ours but is
**not yet licensed**; all rights are reserved by default. The proprietary
material is not ours to license and is not included.

DIRECTV, HR54, Pace, Broadcom, Jellyfin and other names are trademarks of their
respective owners, used for identification only. No endorsement is implied.

→ [LICENSING.md](LICENSING.md)
