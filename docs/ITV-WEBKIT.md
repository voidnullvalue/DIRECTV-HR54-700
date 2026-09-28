# ITV / WebKit TV UI

## The problem

The HR54 has a real browser engine. DIRECTV ships a "Connected TV" (iTV) system
built on a vendor WebKit, and it can render arbitrary pages full screen. If that
could be pointed at a local URL, no separate device, browser, or app would be
needed — the DVR would be its own TV client.

It is the right approach, and getting it to work was the hardest UI problem in
this project.

## Discovery

The iTV system lives under `/opt/itv/` on the receiver, with
`/opt/itv/itvpack/root/lib/libcwebkit.so` providing the engine. Key findings:

- `itvGetAppStatus` reports app state without presenting anything.
- `itvEnableScreen` existed in earlier analysis but was **removed** from this
  build.
- `setDruidProperty` accepted a probe payload but **Druid ignored it** — the
  screen-ownership handshake did not complete.
- A screen-ID attempt (`90712`) did not present.
- `DigitalSignageService.launchApp` failed with a stack trace rather than a
  silent no-op.

Full probe log: `jellyfin/re/itv-presentation.md`. It is a **negative result**
and is kept precisely because it saves someone else the same afternoon.

## The deadlock: DirectTest launch vs Druid ownership

The two obvious launch routes each fail for a different reason:

| Route | What happens |
| --- | --- |
| `DirectTest` launch of the iTV app | The app **starts** (proven — it fetched our page over the LAN), but Druid never transfers presentation ownership, so nothing appears on screen. Screen ID does not change. |
| Druid property / screen-ID approach | Druid only accepts properties from a context that already owns the screen, and nothing in our process can be given that ownership. Deadlock. |

The LAN path into the renderer was proven open: a `python3 -m http.server` on a
dev host serving a minimal page **was fetched successfully by the receiver**.
The renderer could load our HTML. It just could not be made to *show* it.

**This is the dead end.** The iTV/Druid presentation route was abandoned.

## What works instead

The receiver has a **MENU watcher** pattern, and the asset-7 hook gives us a
place to run a persistent process. So:

1. The boot hook starts a watcher.
2. The watcher waits for the user to press **MENU** and leave it idle briefly.
3. It **clears the stock presentation** (the Druid screen showing the guide).
4. It walks Druid to LiveTV.
5. It starts the iTV renderer in **fullscreen** against our local URL.

The MENU press is the trigger that makes Druid yield the screen, and our
process wins the race afterwards. Full screen, no browser chrome, native
resolution.

This is the working approach and it is what ships.

## The frontend

`jellyfin/remote/static/tv/` — `index.html`, `app.js`, `app.css`.

**ES5 only.** The receiver's WebKit is an old vendor build; ES6 constructs
(arrow functions, template literals, `class`, `const` in some positions) parse
unreliably. The app is one file, no build step, no bundler, no npm, no
transpile. What you read is what runs.

Everything is served by `hr54-jf` from `/var/hr54-persist/jellyfin/www/tv/`
over the same origin as the API, so there is no CORS and no second port.

### Features

| Feature | How |
| --- | --- |
| Remote key navigation | TV `keydown` → `POST /api/tv/event` → `GET/POST /api/tv/state` |
| **Search** | local filter over the fetched item set |
| **On-screen keyboard** | custom ES5 keyboard, driven by the remote D-pad — not the browser's, which has no text input focus on this renderer |
| **Paging** | paged `GET /api/items`; virtual locations excluded server-side |
| Artwork | `GET /art/<id>`, cached on the receiver |
| **Saved browse state** | last library/folder/offset persisted, so you resume where you left off |
| **Playback return** | on stop, the UI restores the exact prior screen |

The on-screen keyboard is worth calling out: the obvious approach is an
`<input>` and the browser's own keyboard, but this renderer has no reliable
text-input focus. A custom D-pad keyboard was the only thing that worked.

## ITV dead-end test page

`jellyfin/ui/itv-test.html` is the minimal proof page used to establish that the
renderer can load a local URL. Kept because it is the fastest way to retest
whether a future firmware build fixes the Druid ownership problem — serve it,
point `remoteItvUrl` at it, and see.

## Settings / menu work

Getting a launcher entry into the stock UI was also attempted. Full notes in
`jellyfin/ui/STOCK_SETTINGS.md`; the durable findings:

- The boot-time **FlatBuffers overlay** for `STB_Settings` **works** — the
  receiver picks up an overlaid resource from `/proc/mounts`.
- But the compiled `createMenuRowMap` **hardcodes seven stock section refs**, so
  an eighth, arbitrary row is whitelisted out and never renders. The FlatBuffer
  alone can never be sufficient.
- `remoteItvUrl` / `MiscConfigPanelRemoteItv` is a real lever for pointing iTV
  at a URL, but it also cannot beat the Druid ownership problem.
- `/opt/dtv/dtv.car` is a **CEEJ VM bytecode archive**, not native code as first
  assumed. Patching it means patching bytecode, which risks crashing `siege`.
  Judged not worth it, and the plan was never executed.

Tools: `jellyfin/ui/inspect_settings.py` (a from-scratch FlatBuffers reader —
no vendor code), `add_settings_row.py`, `rename_help_tile.py`,
`walk_settings.sh`, `probe.sh`.

## Screensaver

`assets/screensaver-hax0r.png` is **our own artwork** — a 360×286 RGBA PNG with
"lol hax0r" on solid black. It is deliberately sized to the stock slot so it
drops straight in, and it is deployed by **bind-mounting over** the stock file:

```sh
mount --bind /var/hr54-persist/screensaver-hax0r.png \
      /opt/ui_assets/assetspack/images/screensaver.png
```

The stock file underneath is untouched. `images/alt/screensaver.png` is a
symlink to `../screensaver.png`, so the same mount covers it. It is not baked
into any plugin image — it is delivered separately and mounted.

Stock DIRECTV art is **not** included. Roll back with
`jellyfin/ui/rollback-local-www-and-screensaver.sh`.

## Related

- [JELLYFIN.md](JELLYFIN.md) — the backend that serves this
- [PLAYBACK.md](PLAYBACK.md) — playback and the return-to-UI path
- `jellyfin/re/itv-presentation.md` — the full presentation dead end
- `jellyfin/ui/STOCK_SETTINGS.md` — Settings FlatBuffer investigation
- [DEAD-ENDS.md](DEAD-ENDS.md)
