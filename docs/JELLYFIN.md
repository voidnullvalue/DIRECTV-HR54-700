# Jellyfin

## What the appliance is

A Jellyfin client that runs **on the receiver itself**. No development host, no
companion server, no bridge process. The HR54 boots, a boot hook starts a
single static C binary, and that binary serves a TV browser UI and proxies
MPEG-TS into the vendor's hardware decoder.

```
HR54 <your-hr54-ip>:8130  ──HTTP──▶  Jellyfin <your-jellyfin-server>:8096
```

`hr54-jf` is a ~87 KB single-file C program with **no dynamic dependencies**,
cross-compiled MIPS32 static. It is the only long-running process added by this
project.

## Layout on the receiver

| Component | Location |
| --- | --- |
| Native backend | `/var/hr54-persist/jellyfin/bin/hr54-jf` |
| TV assets | `/var/hr54-persist/jellyfin/www/tv/` |
| Config + token | `/var/hr54-persist/jellyfin/config/` (dir `0700`, token `0600`) |
| Browse state | `/var/hr54-persist/jellyfin/state/` |
| Artwork cache | `/var/hr54-persist/jellyfin/cache/` |
| PID / log | `/var/hr54-persist/jellyfin/jf.pid`, `log/jf.log` |
| API + UI | `http://<your-hr54-ip>:8130/` |

## Build

```sh
zig cc -target mips-linux-musleabi -mcpu=mips32 \
  -static -O2 -o jellyfin/remote/bin/hr54-jf \
  jellyfin/remote/hr54_jf.c
```

`-mcpu=mips32` is **required**. See [HARDWARE.md](HARDWARE.md#build-for-mips32-never-mips32r2)
and [DEAD-ENDS.md](DEAD-ENDS.md).

## Running it

```sh
/var/hr54-persist/jellyfin/bin/hr54-jf \
  /var/hr54-persist/jellyfin/www <your-jellyfin-server> 8096 8130
```

| Arg | Meaning |
| --- | --- |
| 1 | web root (`www/tv/` holds the TV frontend) |
| 2 | Jellyfin host |
| 3 | Jellyfin port |
| 4 | listen port (production: **8130**) |

It is started by the persistent `/var` boot hook — see
[PERSISTENCE.md](PERSISTENCE.md). Do not also start it by hand; the hook is
the supported path and handles restart.

## Authentication: Quick Connect

The TV shows only the public six-character code. Everything sensitive stays in
the backend.

```
1.  TV   POST /api/auth/start
2.  jf   POST {server}/QuickConnect/Initiate?deviceId=...     -> Secret, Code
3.  TV   displays Code only                                   (6 chars, public)
4.  user authorises Code in the Jellyfin app / web UI
5.  TV   GET  /api/auth/poll
6.  jf   POST {server}/QuickConnect/Connect?Secret=...       -> AuthorizeToken
7.  jf   POST {server}/Users/AuthenticateWithQuickConnect
        {Secret=..., AuthorizerToken=...}                     -> AccessToken, User
8.  jf   persists AccessToken to config/token, mode 0600
9.  TV   GET /api/auth/status -> authenticated, user name
```

The `Secret` and the resulting `AccessToken` **never reach the TV page, a log, a
URL, or the receiver-side process list.** The TV sees the code and a session
handle, nothing else.

`jellyfin/client/auth.py` is the clean Python reference for this flow, and
`hr54_jf.c` is the production implementation. Both were written to the same
contract; the C one is what ships.

Sign-in was verified to **survive a controlled reboot** — the token is read
back from `config/token` at startup and revalidated against `Users/Me`; if
validation fails it is cleared and the TV is asked to sign in again.

## API

| Endpoint | Purpose |
| --- | --- |
| `GET /api/status` | playback state (idle / playing / paused) |
| `GET /api/auth/status` | auth state, user name, pending code |
| `POST /api/auth/start` | begin Quick Connect |
| `GET /api/auth/poll` | poll for approval |
| `POST /api/auth/logout` | clear token, delete `config/token` |
| `GET /api/libraries` | top-level views |
| `GET /api/items` | browse, search, paging, virtual-location exclusion |
| `GET /art/<itemId>` | artwork, cached locally |
| `POST /api/play` | start playback |
| `POST /api/transport` | pause / resume / stop |
| `POST /api/seek` | seek by offset |
| `GET/POST /api/tv/state` | saved browse state |
| `POST /api/tv/event` | remote key events from the TV |
| `GET /play/<token>.ts` | **the media relay** |

## The TV frontend

`jellyfin/remote/static/tv/` — `index.html`, `app.js`, `app.css`.

Deliberately **ES5**. The receiver's ITV renderer is an old vendor WebKit
build; ES6 syntax (`let`/`const` in some positions, arrow functions, template
literals, classes) is unreliable there. The app is a single file with no
build step, no bundler, and no npm.

Features, all local frontend backed by `hr54-jf`:

- library and folder browsing
- **search with an on-screen keyboard** driven by the remote
- paging
- artwork via the local cache
- **saved browse state**, so returning from playback restores where you were
- return-to-UI from playback
- full-screen, no browser chrome

`jellyfin/remote/static/{index.html,app.js,app.css}` (the non-`tv/` set) is the
earlier development UI, kept for reference. The TV set is the shipping one.

## Tests

```sh
jellyfin/remote/test/run_host_tests.sh
```

Compiles `hr54_jf.c` natively and runs 37 assertions against
`mock_jellyfin.py` and `mock_fetch.py`:

- auth gate rejects unauthenticated requests
- Quick Connect code shown, pending before approval, authenticated after
- libraries, item paging, **virtual-location exclusion**, overview truncation
- search with unicode and ampersand; the mock verifies the decoded term
- TV state round-trip
- artwork fetch + content type + **cache hit (exactly one upstream fetch)**
- `/play` relay requests the upstream stream
- **pause gates the stream; bytes stop flowing while paused**
- resume ungates; bytes flow again
- **long-pause resume restarts the stream (second `PlaybackInfo`)**
- **seek restarts at offset, `StartTimeTicks` appended**
- **stop closes the relay early (less than the full stream)**
- status returns to idle; logout removes the token file

`mock_jellyfin.py` deliberately includes adversarial fixtures — `Shows &
"Things"`, `Händel & Co`, 900-character overviews — because shell/XML escaping
bugs in this code path are the ones that show up only on real library data.

## The superseded host backend

`jellyfin/remote/server.py` and `jellyfin/client/` are the **host-side Python
prototype**. They produced the first successful end-to-end playback and the
`auth.py` Quick Connect reference, and they drive the host test harness.

They are **not** part of the appliance. The shipped system is receiver-native;
the development host is not a runtime dependency and may be powered off.
Port **8131** in any documentation is the retired development port — do not
use it.

## Receiver tools

`jellyfin/tools/` — all talk to the receiver's TCP/5777 root shell. Set
`HR54_HOST` to your receiver's address.

| Tool | Purpose |
| --- | --- |
| `hr54.sh` | run a command, or `-f script.sh` |
| `dtq.sh` | feed XML to the `uconntest` handler |
| `hr54-pull.sh` | pull a file (gzip + hex framing) |
| `hr54-pull-clean.sh` | tolerant variant for truncated pulls |
| `tftpget.py` | TFTP reader, `blksize` 65464 negotiation — bulk pull |
| `hr54-tftp-put.py` | TFTP writer — bulk upload |
| `hr54-playurl2.sh` | dismiss the OSD, invoke `playURL`, slice the log |
| `hr54-matrix.sh` | run `playURL` across N URLs |
| `hr54-stopprobe.sh` | find which command actually stops `playURL` |
| `hr54-state.sh` | Druid screen id, OSD registry, player state, `ps` |
| `receiver-hr54-play-url` | **the production playback wrapper** |

Retired as dead ends: `hr54-hdmi.sh`, `hr54-hdmi2.sh`, `hr54-experiment.sh`,
`hr54-logtail.sh`, `hr54-playurl.sh`. See [DEAD-ENDS.md](DEAD-ENDS.md).

## Device profile

Advertise a conservative profile so the server never transcodes:

MPEG-TS · H.264 up to 1080p · AC3 48 kHz stereo · LAN HTTP · no HLS/DASH.
See [PLAYBACK.md](PLAYBACK.md#working-codec-profile).

## Reference hashes

The author's appliance, as a regression tripwire:

| Artifact | MD5 |
| --- | --- |
| `hr54-jf` (production) | `6a41c72c95d6ec99781cd1f8bb12c223` |
| asset-7 v4 plugin image | `f8cedcc05837d6df1391b4e4f7cfc88a` |
| asset-7 v3 (rollback) | `cf6ae48990bac41b3ce526ea1681ba85` |

## Related

- [PLAYBACK.md](PLAYBACK.md) — the media path and transport control
- [ITV-WEBKIT.md](ITV-WEBKIT.md) — how the TV UI is hosted
- [PERSISTENCE.md](PERSISTENCE.md) — the boot hook
- [ROLLBACK.md](ROLLBACK.md)
