# Host Jellyfin client prototype

`hr54_jellyfin.py` lists videos, requests Jellyfin PlaybackInfo with the HR54 MPEG-TS/H.264/AC3 device profile, proxies the resulting transcode stream, and invokes `/var/opt/hr54/bin/hr54-play-url` on the receiver. The receiver sees only an opaque HTTP URL on the host; Jellyfin credentials stay on the host.

Create a private API-key file outside this repository (`chmod 600`). Then list and play an item:

```sh
python3 hr54_jellyfin.py --server http://SERVER:8096 --key-file /private/key --search 'title'
python3 hr54_jellyfin.py --server http://SERVER:8096 --key-file /private/key --item ITEM_ID
```

The play command keeps its HTTP proxy running in the foreground. Keep that process alive while the movie plays; interrupting it closes the stream. Default proxy port is 8100 on the host LAN address. `--bind`, `--port`, and `--receiver` override the network endpoints.

The first end-to-end item, **A Trip to the Moon**, played on the HR54 with picture and sound physically confirmed. Current limitations: host process must remain running; there is no TV browsing UI, progress reporting, or proven stop/pause/seek command yet. The current receiver-side wrapper clears OSD 36-775 when starting playback.
