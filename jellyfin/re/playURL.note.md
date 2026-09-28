# Generic HTTP playURL

Known working request:

```xml
<com.ucentric.pvruconnect.DirectTest command="playURL" url="http://HOST/media.ts"/>
```

`url` is the only known required attribute. On real HR54 hardware, a LAN HTTP MPEG-TS stream with H.264 and AC3 produced visible SMPTE color bars and audible 440 Hz tone over HDMI. Logs in `jellyfin/tests/logs/ts-h264-ac3-3-osdcleared.log` show MediaPlayer startup, CDI decoder configuration, video/audio enabled, frame presentation, and advancing position. OSD 36-775 had initially hidden the picture; clearing it revealed the already playing video.

Use `/var/opt/hr54/bin/hr54-play-url URL` for new calls. It clears a current 36-775 OSD, sends the wake keys used in the physical playback proof, XML-escapes the URL, and invokes `playURL`. It logs no URL or query token. Jellyfin URLs carry API keys, so `client/hr54_jellyfin.py` gives the receiver an opaque host proxy URL instead. Stop, pause, resume, and seek have no proven API yet. A successful `uconntest` response alone does not establish media playback; use media logs and HTTP requests for verification.
