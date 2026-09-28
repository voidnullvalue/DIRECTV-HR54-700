# Stock Settings menu: live trace

## Boot-time Settings overlay (2026-09-27, proven)

The receiver was rebooted with the asset-7 boot hook enabled. The boot is healthy and the overlay worked.

- `<HR54_IP>` answers `ping` within seconds, but **TCP/5777 and HTTP/8080 do not return for roughly 8-9 minutes.** Do not treat early ping as failure, and do not reboot during that window. Both ports came up together.
- Active plugin image: `md5(/var/network/plugins/7_6933_6840.squashfs) = 1319b53748def1fec0509ce8074be1a1`, matching `jellyfin/ui/build/asset7-jellyfin.squashfs`.
- `/proc/mounts` contains a **file bind mount** over the live resource:

  ```
  /dev/sda2 /opt/ui_assets/assetspack/curation/layouts/STB_Settings.fb xfs rw,noatime,attr2,rtdev=/dev/sda3,noquota 0 0
  ```

  `/var` is on `/dev/sda2`, so the source is `/var/hr54-persist/jellyfin/ui/STB_Settings.jellyfin.fb`. The apparent "xfs filesystem" is an artifact of BusyBox copying the source superblock options for bind mounts; it is a bind mount, not a mount of `/dev/sda2` itself.
- Live resource MD5 is `60da874406400a88c8e1b30cdec3cdb3`, identical to the patched host file and to `/var/hr54-persist/jellyfin/ui/STB_Settings.jellyfin.fb`. The original `cbb6cba7abc044b0c6ae58f5561f03cd` remains at `STB_Settings.original.fb`.
- The mount happens **before** Druid starts, and this is what the earlier inconclusive test missed. `messages.log` shows the middleware starting at `05:13:21` and the Settings layout loading at `05:18:14` with `ScreenLoadTimeTrackerService - ... LayoutName=Settings, ScreenType=Legacy, NavSource=Menu`. Druid therefore read the patched file, not a pre-cached one. The earlier "Druid caches layouts" conclusion is retired; caching was a side effect of mounting after startup.
- `siege` is pid 3705 with Druid, ITV/WebKit and `dvr_core` alive. `pidof druid` is empty because Druid runs inside `siege`. **No manual `siege` kill was performed in this phase.**

`/var/hr54-beachhead/jellyfin-settings-mount.log` exists but is 0 bytes; the hook does not log on this path. Rollback remains `/var/hr54-persist/jellyfin/ui/rollback` (mode 0700).

## Live navigation on 10306

Boot lands on LiveTV screen 2320. A local SHEF `menu` keypress reaches 10306 (`LegacyMenuScreen`) with no OSD obstruction.

Walking the Settings row and pressing SELECT at each position produced one decisive trace:

```
druid - [DRUID] [GGUP] BaseRowActionHandler(S<UiSession id="0" ip="127.0.0.1">):
    handleTileScreenData() [HAI] screenId=1016
java.lang.ArrayIndexOutOfBoundsException
    at com.directv.druid.util.action.BaseRowActionHandler.lambda$handleTileScreenData$5(...)
    at com.directv.druid.util.action.BaseRowActionHandler.handleTileScreenData(...)
```

SELECT on the initially focused cell stays on 10306 with no handler trace. SELECT on the first content cell resolves to **screen 1016** (`GuidedSetupSignalStrengthScreen`). `down` is rejected with the `BONK!` sound, so the settings tiles form a single row.

## Full layout schema (decoded from the original resource)

A complete signed-offset FlatBuffers walk of the stock file gives the real table shape. Each entry is far smaller than expected:

```
PAGE.f0        = 'STB_Settings'
PAGE.f2        = vector of 7 sections
  SEC.f0       = section reference string
  SEC.f1       = vector of 1 block
    BLK.f0     = properties table
    BLK.f1     = block id string, e.g. 'stb_setting_conn'
    BLK.f2     = label table
      LBL.f0   = English label
      LBL.f1   = Spanish label
      PROPS.f0 = CTA JSON ([{"primary":[{"intent":"play-non-linear",...}]}...])
      PROPS.f3 = type, 'static'
      PROPS.f13= category, 'stb-settings'
```

**There is no action, screen-id, intent, or URL field anywhere in the layout.** `STB_Apps.fb` uses the same three properties fields and likewise carries no payload. A curation layout can therefore supply a **label only**; every action is resolved in compiled code. The CTA vocabulary was checked in `dtv.car` and contains no `open-url` or `launch-app` intent, so the CTA property cannot be used to point at an external renderer.

## Row visibility: measured on the TV, not assumed

The patched resource was live and Druid had read it, yet `STB_Settings_Jellyfin` **does not render**. The user navigated to the far end of the Settings row on the TV and saw only stock tiles. This confirms the compiled `createMenuRowMap` whitelist is the gate and that the FlatBuffers resource can never be sufficient on its own. Do not spend more effort on layout resources.

## The ITV launch path: `remoteItvUrl`

A far better lever exists and needs **no binary patching**.

`/opt/ui_assets/assetspack/config/druid.properties` is a plain, readable properties file on the read-only rootfs. It contains:

```
remoteItvUrl=http://<VENDOR_HOST>:8000/WeatherWidget/CreateImage
```

`dtv.car` shows what consumes it. The class `com.directv.druid.screens.extra.MiscConfigPanelRemoteItv` owns a Settings misc panel labelled **"Remote iTV App"** with fields "iTV App Height:", "iTV App Width:", "iTV App Via Remote URL:", "Number of Images:", and the methods `launchItvAppFromRemoteLocation` and `launchItvApp`. Its panel description reads:

> Enable the GREEN key in Live TV to launch an iTV app obtained from a remote location.

Its property and constant names are `remoteItvEnabled`, `remoteItvUrl`, `remoteItvHeight`, `remoteItvWidth`, `remoteItvNumImages`, `optRemoteItvEnable`, `REMOTE_ITV_ENABLED`, `REMOTE_ITV_URL`, `REMOTE_ITV_APP_HEIGHT`, `REMOTE_ITV_APP_WIDTH`, `REMOTE_ITV_NUM_IMAGES`.

So the vendor ITV/WebKit renderer already has a **stock, config-driven path to load an arbitrary URL**, reachable both from the GREEN key in Live TV and from a Settings misc panel. No `dtv.car` change is needed to prove the minimum ITV case.

Note the split: `remoteItvUrl` in `druid.properties` is a **default**, while the panel writes the live values into the receiver settings store under the `REMOTE_ITV_*` keys. The enable toggle is therefore most likely a persisted user setting rather than a properties-file key, which is why setting it in `druid.properties` alone is not sufficient.

## Test artifacts staged

- `jellyfin/ui/itv-test.html` is the minimum ITV proof page: large "JELLYFIN TEST" heading, an explicit list of the remote keys the receiver must deliver, a live on-screen key-event counter fed by `keydown`, `keypress` and `keyup` listeners, an optional `window.dtv` bridge probe, and an `XMLHttpRequest` to `/itv/ping` to prove scripting and network reachability from inside the renderer.
- It is served from the dev host `<DEV_HOST>:8123` with `python3 -m http.server`. **The receiver fetched it successfully**, so the LAN path from the set-top into the renderer is open.
- A patched properties file is staged at `/var/hr54-persist/jellyfin/ui/druid.properties.jellyfin` (MD5 `517e65b985c189612e756529e2e25dcc`), with the untouched original preserved at `druid.properties.original` (MD5 `b3dba5a77160405d8b8ff75f79b25f8a`, identical to the on-box file). It sets `remoteItvUrl=http://<DEV_HOST>:8123/itv-test.html` plus `remoteItvEnabled=true` and `optRemoteItvEnable=true`.
- It **is currently bind mounted live** and Druid still ignored it, which is the expected result: Druid reads properties once at startup. Re-proving that Druid caches startup state a second time is unnecessary; the fix is to have the overlay in place before the middleware starts, exactly as the Settings layout required.
- A live bind mount alone is not a durable solution, so the asset-7 wrapper was extended with a second conditional branch, `enable-itv-overlay` plus `druid.properties.jellyfin`, mirroring the existing settings branch. Rebuilt image: `jellyfin/ui/build/asset7-jellyfin-v2.squashfs`, MD5 `09d4626df7d72535eeb2d636ec9a3b0f`, SHA-256 `1c2388df05ae793a0256884f65c3f9af7a169356a681d24e4c48135c2a42c784`. `indexer.real` is verified byte-identical at MD5 `68000c562795a45ea6100bc5b070ee17`, so only the launcher wrapper differs.

### Why the image has not been swapped yet

`/var/network/plugins/7/current` is a **read-only loop mount** of `7_6933_6840.squashfs`, so the wrapper cannot be edited in place; the image itself must be replaced. The receiver has **no `mksquashfs`, no `unsquashfs`, no `base64`, no `python`, no `perl`, no `openssl`, and a BusyBox `hexdump` without `-r`**, so the 2.6 MB image has no convenient decoder on the far side. Delivering it needs an octal `printf` framing over the root shell, which is slow and fragile.

There is a cheaper route worth trying first. The "Remote iTV App" panel is a **Settings misc panel**, and a panel that writes `remoteItvUrl` and the enable flag into the receiver settings store persists across reboot on its own, needing no image work at all. It is reached from Settings through the Misc. Options tile. Software navigation cannot get there because the first content tile routes to screen 1016, which swallows every subsequent key in the no-satellite state, so this one step has to be done on the TV.

## Compiled routing, from dtv.car

`extracted/sdb4-rootfs/opt/dtv/dtv.car` is a concatenated container; its string pools are plaintext and directly readable.

- `com.directv.druid.screens.menu.curated.MenuRowProvider` contains `createMenuRowMap` with **at least 17 lambdas** and hardcoded section-reference constants: `STB_Settings_Start_Up`, `STB_Settings_Connectivity`, `STB_Settings_Misc_Options`, `STB_Settings_Device_Management`, `STB_Settings_Help`, `STB_Settings_Preferences`, `STB_Settings_Setup_Configuration`. All seven stock refs are compiled in. `STB_Settings_Jellyfin` is absent.
- The block ids `stb_setting_conn` and friends **do not appear in dtv.car at all**, so the action map is keyed by the *section reference*, not the block id.
- `RowActionHandler` dispatch is by user-data type, not by section: `handlePluginServiceData`, `handleSelectNativeAppData`, `handleServiceUserData`, `handleTvAppServiceData`, `handleSpecialScreenIds`, `handleTileScreenData`, plus `launchPluginService` and `lambda$launchItvApp$9`.
- `handleTvAppServiceData` is the app launcher and already carries six hardcoded ITV widget URLs, for example `http://iw.dtvce.com/widgets/mfwfs1.0/index.htm` and `https://iw.dtvce.com:8443/widgets/accuweather/index_webkit.htm`, with an `isWidgetItvApp` test. **This is the existing mechanism that starts the vendor ITV/WebKit renderer on a URL, and it is the target for launching a Jellyfin page.**
- `MenuPluginServiceBuilder` is a `Hashtable<Integer, IMenuPluginServiceFactory>` with `addPluginService` / `removePluginService` / `getPluginService` and constants `PANDORA_SERVICE`, `RECOMMENDATION_SERVICE`. `IMenuPluginServiceFactory` exposes `getSearchFunction`, `getFindByResults`, `launchPluginService`. This is a search and app-launch registry, **not** a Settings-row injector, and populating it requires executing Java inside the Druid process.

## Why a FlatBuffers row alone cannot work

The layout supplies labels; `createMenuRowMap` supplies both the row implementation and the action, from a compiled list of the seven stock section references. A new section reference therefore has no compiled row, and a block id is not a key anything looks up. The patched resource is live and valid, but visibility of the `STB_Settings_Jellyfin` row is still **unproven**, and a working SELECT action is known to be impossible without a code change.

## Delivery mechanism for a code change

`/` is a **read-only squashfs** (`/dev/root on / type squashfs (ro,relatime)`), so `dtv.car` cannot be edited in place. It is readable and copies cleanly, and there is **no signature file beside it** in `/opt/dtv/`. The file bind-mount technique already proven for `STB_Settings.fb` applies unchanged, so a modified `dtv.car` can be delivered through the same asset-7 boot hook that just succeeded.

## Dead ends, recorded so they are not retried

- The `java.lang.IndexOutOfBoundsException: Index: 1, Size: 1` in `MenuScreen.getEntryString` during `vocalizationOnHighlightGained` is **pre-existing**. It appears in `messages.log.098/099/100.gz` from unpatched boots. It is not evidence about the new row.
- The TTS `stringIds` list is not row-discriminating. Stock and patched boots both log `stringIds: [200004000, 200032900, 200033000]` on entry to 10306, and the list does not enumerate the tiles.
- `ScreenLoadTimeTrackerService` reports `FirstVisibleIndex=0` regardless of row count.
- No SHEF route returns on-screen text. `/remote/getDisplayedText`, `/remote/getScreenText`, `/remote/getCurrentScreen`, `/remote/getWidgetTree`, `/remote/getOSD`, `/remote/getPageInfo` and others all return 404. `/itv/app` returns 403 to an unauthenticated local GET, which does **not** establish that custom ITV apps cannot run.

## Rollback

The overlay is controlled by the presence of `/var/hr54-persist/jellyfin/ui/enable-settings-overlay` in the asset-7 hook. `/var/hr54-persist/jellyfin/ui/rollback` deletes the flag, unmounts the resource, restores `asset7-before-jellyfin.squashfs` from `/var/hr54-persist/backup/`, and syncs. Host source: `jellyfin/ui/receiver-rollback`. Host pre-test image backup SHA-256 `ed2e8bb7761d0ff95dc44083e480f8a196a987d22d659b128d941a8590e3b315`; `indexer.real` was verified byte-identical between the two images, so only the launcher wrapper differs.

## Second pass: what `dtv.car` actually is, and why the patch was dropped

Recorded 2026-09-27 so this is not re-derived.

**`dtv.car` is not native code.** `/opt/dtv/dtv.car` (19,912,650 bytes) starts
with magic `ce ec ac cc` followed by a plain-text description of a line-based
**closed-captioning** format (`[SERVICE=1,CHANNEL=500]`, `/xNN` escapes). Past
that text block the file is a **CEEJ class archive**. CEEJ is a commercial
Java-like bytecode VM by Skelmir, LLC; `/opt/vm/lib/libsiege.so` is the
VM and interpreter (ELF 32-bit MSB, **MIPS32 big-endian**, stripped) and holds a
`ceej_system_car` section. The VM exposes `-Xint`, `-Xnojit` and
`-Xhexoffset  print bytecode offsets in hex`.

Consequences:

- The Druid application logic is **VM bytecode in a proprietary class format**,
  not MIPS machine code. There is no `CAFEBABE`, so no standard class tooling
  applies and the format must be reverse-engineered from scratch.
- The three `dex\n` hits inside `dtv.car` are false positives from string data
  (`...index\n`), not embedded DEX files.
- `createMenuRowMap`, `MenuRowProvider` and `BaseRowActionHandler` are CEEJ
  classes `com/directv/druid/screens/menu/curated/MenuRowProvider` and
  `com/directv/druid/util/action/BaseRowActionHandler`.
- Patching them means editing VM bytecode, with a real chance of crashing
  `siege`, the process that renders the entire UI and which must not be killed.
  Recovery would cost a ~9 minute reboot. That risk was judged not worth it.

**Layout format, now fully decoded.** Each `*.fb` is a FlatBuffer of
`response -> page -> sections[]`, and each section carries the provider name in
field 0, one block, a label, and a props record. Stock `STB_Settings.fb` has
exactly seven sections whose names match the seven `STB_Settings_*` strings
compiled into the VM, plus a separate `STB_Settings_` prefix constant. A new
invented provider such as `STB_Settings_Jellyfin` therefore has no compiled row,
which confirms the earlier diagnosis. The CTA JSON holds only playback intents
(`play-non-linear`, `play-linear`, `default`, `record`) and **cannot** carry a
URL, so a layout can never launch a local page by itself.

**There is no way to see the TV.** `/dev/video0` and `/dev/video1` on the host
are the laptop's integrated webcam; there is no HDMI capture device. Every UI
change can only be judged indirectly, via screen IDs, log strings and HTTP
hits. That alone blocks validating any label or row change. No SHEF route
returns on-screen text.

**Current direction (corrected).** The on-TV entry is the product requirement;
the host remote is only its backend and fallback. Prefer proving ITV/WebKit and
repurposing an existing compiled Apps tile before editing CEEJ control flow.
Direct CEEJ work remains in scope if those smaller hooks fail.
