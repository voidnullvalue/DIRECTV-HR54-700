# Jellyfin TV UI state

## Row visibility is settled: the layout cannot do it

The boot-time FlatBuffers overlay mechanism **works**, and it is now the reliable delivery mechanism. After the reboot the patched `STB_Settings.fb` was bind mounted before Druid started, Druid read it when Settings opened, and the resource on screen was the patched one. The earlier conclusion that Druid caches layouts is retired; the old test failed only because it mounted after startup. Details and hashes are in [STOCK_SETTINGS.md](STOCK_SETTINGS.md).

The user then looked at the TV: **`STB_Settings_Jellyfin` does not render.** Only stock tiles appear. The compiled `createMenuRowMap` whitelist is the gate.

Three facts, all now measured rather than assumed, close off the layout route:

1. **A curation layout carries labels only.** The full FlatBuffers schema was decoded. There is no action, screen-id, intent or URL field in `STB_Settings.fb` or `STB_Apps.fb`. Every action is compiled.
2. **`createMenuRowMap` hardcodes the seven stock section references** and has 17+ lambdas. Block ids are not lookup keys anywhere in `dtv.car`.
3. Therefore a new section reference gets no row, and adding one more FlatBuffers row cannot change that.

## The route that needs no binary patching

`/opt/ui_assets/assetspack/config/druid.properties` carries `remoteItvUrl`, and `dtv.car` shows it belongs to `MiscConfigPanelRemoteItv`, a **Settings misc panel called "Remote iTV App"** whose own description says it enables the GREEN key in Live TV to launch an iTV app from a remote location. The vendor ITV/WebKit renderer therefore already has a stock, config-driven way to load an arbitrary URL, reachable from the GREEN key and from Settings.

That is a far better lever than patching `dtv.car`, and it is enough for the minimum ITV proof.

Progress on the minimum ITV proof:

- `jellyfin/ui/itv-test.html` is written: visible heading, explicit key list, on-screen key-event counter from `keydown`/`keypress`/`keyup`, an optional `window.dtv` bridge probe, and an `XMLHttpRequest` to `/itv/ping`.
- It is served from the dev host `<DEV_HOST>:8123`, and **the receiver fetched it successfully**, so the LAN path into the renderer is open.
- A patched `druid.properties` is staged on the receiver with the original preserved, and is currently bind mounted live. Druid ignored it, as expected, because it reads properties only at startup.
- The asset-7 wrapper has been extended with a matching `enable-itv-overlay` branch and rebuilt as `jellyfin/ui/build/asset7-jellyfin-v2.squashfs` (MD5 `09d4626df7d72535eeb2d636ec9a3b0f`), with `indexer.real` verified unchanged. It has **not** been swapped onto the receiver yet.

Two blockers are known and neither needs more reverse engineering. `/var/network/plugins/7/current` is a read-only loop mount, so the image must be replaced wholesale, and the receiver has no `mksquashfs`, `base64`, `python`, `perl`, `openssl`, or a `hexdump -r` to decode the 2.6 MB image. Cheaper still, the "Remote iTV App" panel writes its values into the receiver settings store, which persists on its own with no image work.

## Not done

No Jellyfin entry is confirmed visible in the stock Settings UI, and none is achievable from the layout resource alone. SELECT launches nothing of ours. No ITV page has been rendered from a local URL yet, though the receiver can reach one. No Jellyfin TV browser exists. No Quick Connect sign-in has been completed. The playback backend in `jellyfin/client/hr54_jellyfin.py` is untouched and remains the proven path.

## Next step

Prove the minimum ITV case, in this order: text visible, remote keys delivered, then EXIT returning to the stock Menu. The cheapest way to arm it is the "Remote iTV App" panel under Settings, reached from the Misc. Options tile, which software navigation cannot reach because the first content tile routes to screen 1016 and swallows keys in the no-satellite state. Once a local page renders under remote control, the "Jellyfin" label in Settings is a separate, smaller problem than it currently looks, because the launch mechanism will already exist.

