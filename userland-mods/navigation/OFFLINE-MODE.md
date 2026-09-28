# Offline Normal Mode

`/var/opt/hr54/bin/hr54-normal-mode enable` creates a persistent policy flag and starts a watcher. The watcher waits until the Druid local screen query succeeds and the OSD registry exists. It removes only OSD number 36 with extension 775, retries the Menu request until screen 10306 is reached, and checks every two seconds for recreation of that same OSD. It leaves all other OSDs alone. `hr54-home` requests stock Main Menu. `disable` removes the flag and stops the watcher; `status` reports flag, watcher, and current OSD. Disabling does not fabricate an OSD.

Production `rcS::configure_var_opt` deletes `/var/opt` whenever `/opt.tar` is absent (lines 374–376), so durable commands and the enable flag live under `/var/hr54-persist`. Asset-7's existing `mp4lib/bin/indexer` boot wrapper recreates `/var/opt/hr54/bin` from those copies after stock cleanup and starts the policy when `/var/hr54-persist/normal-mode.enabled` exists. The stock indexer remains `indexer.real`. The receiver's vendor middleware and SquashFS root remain unchanged. The image was staged under `/var/network/plugins/7_6933_6840.squashfs` via atomic rename; the verifier signature continues referencing the untouched genuine asset 7 anchor.

## Image record and rollback

- Source active plugin image before this change: `/var/network/plugins/7_6933_6840.squashfs`, SHA256 `1d8738b02b575e7f7a117b8fed17c1b16f8e8a08362e42531e42a2fcf9722970`.
- Receiver backup: `/var/hr54-persist/backup/7_6933_6840-v3.squashfs` (MD5 `780340754851982ee194660d69c63b7c`).
- Intermediate image, which exposed the `/var/opt` cleanup: SHA256 `5f6bf76828eb4974728e2239f497f22b994b311125559cc1770793413cea26ef`.
- Current image SHA256: `4a0f253ca721cf642a5fa17b797af38bc65ef116dc85b06530cfd8e823237f6e`; receiver MD5 checked against host: `34904ba07760a87a3c54d81430017b46`.
- Change inside current image: `mp4lib/bin/indexer` restores the commands from `/var/hr54-persist/bin` and conditionally launches normal mode inside the existing once-per-boot branch. No vendor binary was changed.
- Runtime target: asset 7 mounts at `/opt/mp4lib`; no bind mount of vendor files is used for this policy.
- Rollback: run `/var/opt/hr54/bin/hr54-normal-mode disable`; atomically replace `/var/network/plugins/7_6933_6840.squashfs` with a copy of `/var/hr54-persist/backup/7_6933_6840-v3.squashfs`; reboot. Keep the unchanged `.sig` and genuine anchor in place.

The revised policy was enabled live and reached screen 10306 with one watcher and no OSD. Clean-boot suppression remained unverified. The attempted 775 text patch did not change the displayed message after reboot. The user redirected work to Jellyfin: normal mode is now disabled, the language overlay is removed, and `hr54-play-url` clears 36-775 at playback time.
