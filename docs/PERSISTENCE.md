# Persistence

The beachhead gives you root for a few seconds at one predictable point in boot
([ROOT.md](ROOT.md)). This is how the install survives a power cycle and becomes
a permanent appliance.

## The problem: `/var` is cleaned

On every boot the receiver cleans `/var/opt` — which is exactly where a naive
install would land. Anything written there is gone by the time you would use
it. The original project hit this and it is the reason a *durable* directory
was needed.

## The solution: `/var/hr54-persist`

The receiver preserves `/var/hr54-persist` across reboots. Nothing in the stock
startup removes it. That is the substrate.

```
/var/hr54-persist/
├── backup/                          pre-change images, for rollback
├── inventory/                       first-boot hardware inventory
├── jellyfin/
│   ├── bin/hr54-jf                  native backend
│   ├── www/tv/                      TV frontend (index.html, app.js, app.css)
│   ├── config/config.json           non-secret config
│   ├── config/token                 Jellyfin access token, mode 0600
│   ├── state/tv-state.json          saved browse state
│   ├── cache/                       artwork cache
│   ├── jf.pid
│   └── log/jf.log
├── transfer/                        staging area for uploads (mode 0700)
└── boot/                            the boot hook
```

Permissions are set at creation: directories `0700`, the token file `0600`.
**`/var/hr54-transfer` in particular is a world-writable-somewhere staging area
on a box with an unauthenticated root shell — keep it mode `0700`.**

## The boot hook

The hook is the `indexer` wrapper from
[linux-port/beachhead/indexer](../linux-port/beachhead/indexer), installed as
`mp4lib/bin/indexer` inside the asset-7 plugin image. On every boot the
middleware's MP4 feature probe executes it, and it starts everything.

The once-per-boot guard matters:

```sh
if /bin/mkdir /tmp/hr54-beachhead.starting 2>/dev/null; then
    # ... start collect-inventory, firewall, hr54d, tftpd ...
fi
exec /opt/mp4lib/bin/indexer.real "$@"
```

`mkdir` is atomic and RAM-backed. Repeated probes — and there are several — still
delegate to the genuine vendor binary but do not duplicate firewall rules or
listeners. The guard resets naturally on reboot because `/tmp` does.

`hr54d` (TCP/5777 root shell) is started here. `hr54-jf` is started from the
persistent hook once the network is up.

## Startup order

```
flash init
  └─ rcS
       ├─ network up
       ├─ install_plugin.sh
       │    └─ mounts our asset-7 image
       │         └─ Mp4libFeatureStarter probes mp4lib/bin/indexer
       │              └─ hook runs:  inventory, firewall, hr54d, tftpd
       ├─ middleware starts (siege, dtvwm, dvr_core)
       └─ persistent hook:  /var/hr54-persist/jellyfin/bin/hr54-jf \
                                  /var/hr54-persist/jellyfin/www \
                                  <jellyfin> 8096 8130
```

Verified from a cold boot **with the development host powered off**: root
TCP/5777 came up, WiFi associated, the native 8130 listened, the Jellyfin
server was reachable, the token was loaded from disk, and MENU launched the
fullscreen UI. This is the actual proof that the appliance is self-contained.

## What persists vs what does not

| Persists across reboot | Does not |
| --- | --- |
| `/var/hr54-persist/**` | `/var/opt/**` (cleaned every boot) |
| The asset-7 plugin image in partition 2 | `/tmp/**` (RAM) |
| The token file | The once-per-boot `mkdir` guard (intentional) |

## The asset-7 hook is reversible

Because the hook lives in the plugin image and the previous image is kept, the
entire mechanism is undone by putting the old image back. Nothing in flash is
touched at any point.

```sh
# see ROLLBACK.md for the full procedure
cp /var/hr54-persist/backup/asset7-jellyfin-v3-pre-v4.squashfs \
   /var/network/plugins/7/.next
# verify, sync, atomically rename, reboot
```

## Never

- **Never write flash/MTD.** The trust boundary is intact by design; do not
  erode it.
- **Never touch partition 4** (SWDL staging) or the partition table.
- Do not put anything secret in `/var/hr54-transfer`; assume it is readable.
- Do not add dependencies to the hook on `/tmp` or on any external utility — the
  first action should stay a shell builtin, for the same reason it is in the
  wrapper: it is the one thing that cannot fail.

## Related

- [ROOT.md](ROOT.md) — getting the hook there
- [ROLLBACK.md](ROLLBACK.md) — removing it
- [PROPRIETARY_INPUTS.md](PROPRIETARY_INPUTS.md) — what you must supply yourself
- `linux-port/beachhead/collect-inventory` — the first-boot hardware capture
