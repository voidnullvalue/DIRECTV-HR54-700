# Architecture

The HR54-700 is a 1 TB DIRECTV DVR whose **hard disk is not trusted to boot the
operating system**. The trusted OS lives in motherboard flash; the disk holds
`/var`-class data plus a *signed plugin* chain. That asymmetry is the whole
story of this project: you cannot edit the OS, but you *can* influence which
plugin image gets mounted, and that is enough to get a persistent root.

## The one-paragraph version

A filename/path confusion in the DIRECTV plugin installer lets a crafted asset
image be substituted for the signed one without a valid signature. The
substituted image's `mp4lib/bin/indexer` is a shell wrapper, so the first time
the middleware probes for MP4 feature availability it executes attacker code as
root. That code installs a persistent hook into `/var`, which survives reboot,
starts a native C backend, and that backend serves a TV browser UI and proxies
MPEG-TS from a Jellyfin server into the Broadcom hardware decoder.

## Components

```
        ┌──────────────────────── HR54-700 ────────────────────────┐
        │                                                            │
  flash │  CFE ──► kernel ──► init ──► rcS ──► install_plugin.sh      │
   MTD  │                                        │                   │
        │                          ┌─────────────▼──────────────┐    │
        │                          │  asset-7 plugin SquashFS   │    │
        │                          │  (genuine image + our      │    │
        │                          │   indexer wrapper)         │    │
        │                          └─────────────┬──────────────┘    │
        │                                        │ exec              │
        │  ┌─────────────────────────────────────▼──────────────────┐ │
 /var  │  │ indexer  →  hr54d (TCP/5777 root shell)                 │ │
  disk  │  │          →  persistent /var/hr54-persist hook         │ │
        │  │          →  indexer.real  (genuine vendor binary)     │ │
        │  │                                                      │ │
        │  │  hr54-jf  (native, MIPS32 static)      :8130         │ │
        │  │    ├── TV frontend  (ES5, ITV/WebKit)  ───────────┐  │ │
        │  │    ├── Quick Connect (secret stays here)          │  │ │
        │  │    └── /play/<opaque>.ts  MPEG-TS relay          │  │ │
        │  └──────────────────────────────────────────────────┼──┘ │
        │                                                      ▼    │
        │  hr54-play-url  →  uconntest playURL  →  dvr_core        │
        │                       →  MediaPlayer.watchRemoteStream   │
        │                       →  Broadcom CDI  →  HDMI           │
        └───────────────────────────────────────────────────────────┘
                                                                    │
              ┌─────────────────────────────────────────────────────┘
              │  HTTP GET /play/<opaque>.ts
              ▼
      Jellyfin server  (PlaybackInfo, MPEG-TS H.264 + AC3)
```

## Data paths

### Control path (browse, search, sign-in)

```
TV remote → ITV/WebKit renderer → fetch() → hr54-jf:8130 → Jellyfin HTTP API
```

`hr54-jf` is a single self-contained C file with no dynamic dependencies. It
holds the Jellyfin access token in memory and persists it to a `0600` file
under `/var/hr54-persist/jellyfin/config/`. The TV page only ever sees a
six-character Quick Connect code and an opaque session handle.

### Media path

```
Jellyfin → PlaybackInfo → MPEG-TS (H.264 + AC3)
        → hr54-jf /play/<opaque>.ts   (token-hiding local relay)
        → hr54-play-url
        → uconntest playURL
        → MediaPlayer.watchRemoteStream
        → dvr_core  (Broadcom CDI / NEXUS hardware decoder)
        → HDMI
```

The token-hiding relay exists so the Jellyfin URL and its access token never
appear in a receiver-side command line, log, or on-screen URL. See
[PLAYBACK.md](PLAYBACK.md).

## Why the hardware decoder, and why it matters

The HR54's Broadcom SoC decodes H.264 video and AC3 audio in hardware. That
means playback needs no software transcoding anywhere: the Jellyfin server
streams, and the receiver decodes. This is what makes a 1.3 GHz MIPS box from
2015 able to play 1080p. It also constrains the accepted profile — see
[PLAYBACK.md](PLAYBACK.md#working-codec-profile).

## Trust boundaries

| Boundary | Status |
| --- | --- |
| Flash MTD (CFE, kernel, rootfs) | **Never touched.** Never write MTD. |
| Genuine plugin signature | **Not forged.** See [ROOT.md](ROOT.md) — this is critical. |
| Genuine vendor binaries | Preserved verbatim as `indexer.real`. |
| `/var` on disk | Fully writable, and the persistence mechanism. |
| TCP/5777 root shell | Unauthenticated. See the warning in [ROOT.md](ROOT.md). |
| `/opt/dtv/dtv.car` | Left unmodified; it is a CEEJ VM bytecode archive. |

## Repository layout

| Path | What it is |
| --- | --- |
| `jellyfin/remote/` | Native C backend, TV frontend, tests |
| `jellyfin/client/` | Python Quick Connect + auth reference |
| `jellyfin/tools/` | Receiver interaction, file transfer, playback probes |
| `jellyfin/ui/` | Settings FlatBuffer tools, screensaver, menu launcher |
| `jellyfin/re/` | Raw reverse-engineering notes from discovery |
| `wifi/` | NVRAM reverse engineering and WiFi provisioning tools |
| `asset7/` | Plugin build, offline verification, install and rollback |
| `linux-port/beachhead/` | `hr54d` root-shell daemon, `indexer` wrapper |
| `userland-mods/` | DVR/subscription-gate research (mostly negative results) |
| `tools/` | Forensics and crypto/plugin analysis helpers |
| `pvr/` | Recording metadata cataloguer |
| `assets/` | Our own generated screensaver artwork |
| `docs/` | All documentation |

## Next reading

- [HARDWARE.md](HARDWARE.md) — platform, storage layout, boot chain
- [ROOT.md](ROOT.md) — the vulnerability and what we did about it
- [PERSISTENCE.md](PERSISTENCE.md) — how it survives reboot
- [PLAYBACK.md](PLAYBACK.md) — the media path, proven end to end
- [JELLYFIN.md](JELLYFIN.md) — the appliance in detail
- [ITV-WEBKIT.md](ITV-WEBKIT.md) — how the TV UI is hosted
- [WIFI.md](WIFI.md) — NVRAM WiFi provisioning
- [DEAD-ENDS.md](DEAD-ENDS.md) — what not to waste time on
- [REPRODUCTION.md](REPRODUCTION.md) — end-to-end order of operations
