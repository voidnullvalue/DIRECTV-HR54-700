# Playback

## The proven path

This is the end-to-end chain, confirmed on hardware with a real 1080p stream:

```
Jellyfin server
  │  POST /Items/{id}/PlaybackInfo
  ▼
PlaybackInfo  →  MPEG-TS (H.264 video + AC3 audio)
  │  GET /Videos/{id}/stream?static=true&mediaSourceId=...
  ▼
hr54-jf  /play/<opaque-token>.ts          ← local opaque relay, hides the token
  │  HTTP GET
  ▼
hr54-play-url                              ← receiver-side wrapper
  │  uconntest playURL "http://127.0.0.1:8130/play/<token>.ts"
  ▼
com.ucentric.pvruconnect.DirectTest
  command="playURL"
  url="http://.../....ts"
  │
  ▼
MediaPlayer.watchRemoteStream
  ▼
MediaPlayerProxy.load
  ▼
dvr_core  →  libdvr::RemoteMediaStream  →  demux
  ▼
Broadcom CDI / NEXUS  (hardware decoder)
  ▼
HDMI
```

The key receiver-side invocation is conceptually:

```xml
<com.ucentric.pvruconnect.DirectTest
  command="playURL"
  url="http://127.0.0.1:8130/play/<opaque-token>.ts"/>
```

`jellyfin/tools/receiver-hr54-play-url` is the production wrapper. It removes
the stock OSD 36/775, sends SHEF `info`/`exit`, XML-escapes the URL, and
invokes `uconntest playURL`. **It never logs or prints a URL containing a
Jellyfin token** — the whole point of the opaque relay.

## Why the token-hiding relay exists

A naive implementation hands the Jellyfin URL straight to `playURL`. Then:

- the Jellyfin access token is in a process argument list, visible in `/proc`
- it is in receiver logs
- it is on screen when the OSD displays the URL
- `hr54-jf` cannot rotate it without a reconnect

`hr54-jf` instead registers an **opaque local token** and serves the stream
from `127.0.0.1`. The real URL and token never leave the backend process. This
is also what makes transport control implementable — see below.

## Why stock DVR playback was abandoned

The stock DVR path is gated on a DIRECTV **subscription entitlement**, not on
any technical capability. The gate is
`SharedAlgorithms.isDvrActionAllowed` → `DvrSubscriber`, and it was proven
closed through **three independent entry points**:

1. the UI key path,
2. a forced `gotoScreenById`,
3. a direct `aplPlayback` call.

All three refuse. The receiver is not entitled for this account, and no
software trick on the box changes that — it is enforced by the card/account,
not by code. See `userland-mods/playback/FINDINGS.md`.

The decision was therefore to **abandon the DVR path entirely** and build a
media path that does not consult DIRECTV's entitlement system at all: our own
content, our own server, the vendor's own decoder.

The DVR research is retained because the negative results are valuable —
[DEAD-ENDS.md](DEAD-ENDS.md).

## Hardware decoder evidence

The Broadcom CDI/NEXUS pipeline is a real hardware decoder, not software. The
evidence:

- The `cdi` kernel module (`/lib/modules/cdi/europa.ko`) is loaded at boot and
  is what `dvr_core` talks to.
- CPU load stays flat during 1080p playback; a software decode of 1080p H.264
  on a 1305 MHz MIPS32 core is not possible.
- The accepted audio path is AC3, which Broadcom silicon decodes natively.
- `jellyfin/tools/hr54-state.sh` captures the player state; the decoder is
  engaged without any userland codec being loaded.

This is why the profile below can be 1080p with no transcoding anywhere.

## Working codec profile

Conservative, chosen so nothing needs transcoding:

| Property | Value |
| --- | --- |
| Container | **MPEG-TS** |
| Video | **H.264** (baseline/main, up to 1080p) |
| Audio | **AC3**, 48 kHz, stereo |
| Transport | LAN **HTTP** |
| Protocol | Plain progressive HTTP (no HLS/DASH required) |

Advertise this from the Jellyfin device profile
(`jellyfin/HR54_DEVICE_PROFILE.md` in the original tree; reproduced in
[JELLYFIN.md](JELLYFIN.md)). Do not offer HLS or DASH to the receiver — the
Jellyfin server must produce a direct MPEG-TS stream.

`jellyfin/re/playViivVideo.md` records that `DirectTest.playViivVideo` also
exists and was never needed. `playURL` is the proven entry point.

## Transport: pause, resume, seek, stop

**This is the subtlest part of the whole project and it deserves to be stated
plainly.**

### The problem

The obvious approach is to call DIRECTV's native DVR pause/stop APIs. It does
not work, and it does not work for a structural reason:

> **`playURL` does not create a `VODCapture`.**

The DVR transport controls operate on a `VODCapture` object. A `playURL` stream
has none, so those calls either fail or return success while doing nothing.
Probing confirmed it: **pause and stop SHEF requests return success without
affecting playback.** A success response here does not mean the framebuffer
stopped. Do not trust the return code.

### What we do instead

Because `playURL` is just an HTTP client pulling a byte stream, **the data
path itself is the transport control surface.** `hr54-jf` gates the
`/play/<token>.ts` response:

| Action | Implementation |
| --- | --- |
| **pause** | stop sending bytes, **keep the socket open** |
| **resume** | continue sending bytes on the same stream |
| **long pause** | on resume, if the pause exceeded a threshold, restart the stream/transcode at the saved time |
| **seek** | close the current stream and re-request at `StartTimeTicks` |
| **stop** | close the stream; the receiver sees EOF and exits |

Keeping the socket open on pause is deliberate: closing it makes the receiver
tear down the player and drop to the menu, which is indistinguishable from
stop. Holding the connection open and withholding bytes freezes the decoder in
place.

The long-pause restart exists because holding a half-dead TCP connection
indefinitely is fragile; past a threshold it is more reliable to re-negotiate.

### Receiver-side key mapping

| Key | TV frontend action | Backend |
| --- | --- | --- |
| OK | play / select | `POST /api/play` |
| Pause | pause/resume toggle | `POST /api/transport {pause\|resume}` |
| Left/Right | seek ± | `POST /api/seek {offset}` |
| Stop / MENU | stop, return to UI | `POST /api/transport {stop}` |
| Info | — | dismissed first |

All of this is covered by the host test suite, which asserts
**pause gating, that bytes actually stop flowing while paused, resume ungating,
that bytes flow after resume, long-pause stream restart, seek offset and
appended `StartTimeTicks`, and early-EOF stop**:

```sh
jellyfin/remote/test/run_host_tests.sh
```

`mock_fetch.py` is the oracle: it stands in for the receiver's decoder fetch, so
a stalled read (pause) and a truncated read (stop) are both directly observable
as file-size behaviour.

## Generic URL playback discovery

The useful generalisation: **the vendor's `playURL` accepts arbitrary HTTP URLs
and hands them to the real hardware decoder.** You do not need DIRECTV's
streaming infrastructure, DRM, or entitlement to use it. Anything that produces
MPEG-TS over HTTP will play.

That single fact is the whole project.

## Verified physical result

On the author's appliance, with the development host powered off:

- cold boot with the native backend only
- Quick Connect sign-in completed on the TV
- 1080p H.264 + AC3 played to HDMI
- pause, resume, long-pause restart, seek, and stop all physically confirmed
- MENU exit returned to the fullscreen UI
- the token persisted across a controlled reboot

## Related

- [JELLYFIN.md](JELLYFIN.md) — the appliance
- [ITV-WEBKIT.md](ITV-WEBKIT.md) — how the UI is hosted
- `jellyfin/re/vendor-media-path.md` — the raw confirmed call chain
- `jellyfin/re/playURL.md` — the `DirectTest` surface
