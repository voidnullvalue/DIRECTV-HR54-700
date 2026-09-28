# HR54 Jellyfin remote — receiver-native backend

The production TV client is fully receiver-native. Nothing about it requires a
development host:

```
HR54  <your-hr54-ip>:8130   ->   Jellyfin  <your-jellyfin-server>:8096
```

`remote/server.py` and the runit definitions in `remote/service/` are
**historical / reference only**. Their crontab launcher entries are disabled.
The shipped appliance runs `hr54-jf` on the receiver itself, started by the
persistent `/var` boot hook — see [../../docs/PERSISTENCE.md](../../docs/PERSISTENCE.md).

## Receiver layout

| Component | Location |
| --- | --- |
| Native backend | `/var/hr54-persist/jellyfin/bin/hr54-jf` |
| TV assets | `/var/hr54-persist/jellyfin/www/tv/` |
| Config/token | `/var/hr54-persist/jellyfin/config/` (dir `0700`, token file `0600`) |
| Browse state | `/var/hr54-persist/jellyfin/state/` |
| Artwork cache | `/var/hr54-persist/jellyfin/cache/` |
| PID/log | `/var/hr54-persist/jellyfin/jf.pid`, `log/jf.log` |
| Production API/UI | `http://<your-hr54-ip>:8130/` |
| Jellyfin upstream | `http://<your-jellyfin-server>:8096/` |

Press MENU and leave it idle briefly. The on-box watcher clears the stock
presentation, walks Druid to LiveTV, and starts fullscreen ITV/WebKit. Browse,
remote keyboard search, paging, artwork, and saved state are local frontend
features backed by `hr54-jf`.

Quick Connect is receiver-native. Only its public six-digit code reaches the
TV page; the secret and the resulting token remain in the native backend. The
token is persisted mode `0600` and was verified across a controlled reboot.

Playback obtains `PlaybackInfo` directly from the Jellyfin server, exposes an
opaque loopback `/play/<token>.ts`, and feeds the Broadcom decoder through
`hr54-play-url`. Pause, resume, long-pause restart, seek, stop, remote MENU
exit, and fullscreen UI return are receiver-native.

## Build

Build **MIPS32 version 1** only:

```sh
zig cc -target mips-linux-musleabi -mcpu=mips32 \
  -static -O2 -o jellyfin/remote/bin/hr54-jf \
  jellyfin/remote/hr54_jf.c
```

> **Do not use `-mcpu=mips32r2`.** An r2 build installs instructions the SoC's
> CPU does not implement and dies with **Illegal Instruction** on the receiver.
> This is not a subtle failure: the binary links and runs fine on a host
> emulator. See [../../docs/DEAD-ENDS.md](../../docs/DEAD-ENDS.md).

Reference hashes for the author's appliance (yours will differ if you change
anything at all — treat these as a regression tripwire, not a target):

- production `hr54-jf` MD5: `6a41c72c95d6ec99781cd1f8bb12c223`
- asset-7 v4 plugin image MD5: `f8cedcc05837d6df1391b4e4f7cfc88a`

Run the host test suite after any C change:

```sh
jellyfin/remote/test/run_host_tests.sh
```

It builds `hr54_jf.c` natively, stands up `mock_jellyfin.py` and
`mock_fetch.py`, and asserts auth gating, Quick Connect approval, browse and
search, artwork caching, `/play` relay, **pause gating, long-pause restart,
seek offset, early-EOF stop**, TV state, and logout token-file removal.

## Boot wrapper

The persistent hook is an `indexer` wrapper — the beachhead from
[../../linux-port/beachhead/](../../linux-port/beachhead/):

```
linux-port/beachhead/indexer          ->  installed as mp4lib/bin/indexer
```

It writes the proof marker, installs the port restriction, launches `hr54d`,
then execs the genuine vendor binary as `indexer.real` so normal vendor
behaviour is preserved. See [../../docs/ROOT.md](../../docs/ROOT.md).

## Runtime invocation

```sh
/var/hr54-persist/jellyfin/bin/hr54-jf \
  /var/hr54-persist/jellyfin/www <your-jellyfin-server> 8096 8130
```

## API

- `GET /api/status`
- `GET /api/auth/status`
- `POST /api/auth/start`
- `GET /api/auth/poll`
- `POST /api/auth/logout`
- `GET /api/libraries`
- `GET /api/items`
- `POST /api/play`
- `POST /api/transport`
- `POST /api/seek`
- `GET/POST /api/tv/state`
- `POST /api/tv/event`
- `GET /art/<itemId>`
- `GET /play/<opaque-token>.ts`

`/play/<token>.ts` is the load-bearing endpoint: it is a **local opaque token**,
not a Jellyfin URL. The real Jellyfin URL and its access token never leave the
backend. Transport control (pause / resume / seek / stop) is implemented by
gating this HTTP byte stream, because `playURL` does not create a `VODCapture`
— see [../../docs/PLAYBACK.md](../../docs/PLAYBACK.md).

## Rollback

The pre-v4 selected image is
`/var/hr54-persist/backup/asset7-jellyfin-v3-pre-v4.squashfs`, MD5
`cf6ae48990bac41b3ce526ea1681ba85`. Stage it as the plugin `.next`, verify,
`sync`, and atomically rename it before reboot. V3 launches obsolete
host-dependent `hr54-www`, so it is an emergency rollback, not supported
standalone operation. **Never write MTD/flash.** Full procedure:
[../../docs/ROLLBACK.md](../../docs/ROLLBACK.md).
