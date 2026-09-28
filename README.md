# DIRECTV HR54-700 → standalone Jellyfin box

I took a DIRECTV HR54-700 Genie DVR, got persistent root on it, found the stock Broadcom playback path, made the vendor ITV/WebKit stack render a usable Jellyfin UI, moved the entire backend onto the receiver, and ended up with a standalone Jellyfin set-top box that talks directly to my Jellyfin server over Wi-Fi.

The long-form writeup is published with GitHub Pages:

**https://voidnullvalue.github.io/DIRECTV-HR54-700/**

The page intentionally follows the same terminal-style look and tone as my Google Glass / `glassterm` writeup.

## End state

```text
DIRECTV remote
      |
      v
HR54 ITV/WebKit UI
      |
      v
receiver-native hr54-jf :8130
      |
      v
Jellyfin server :8096
      |
      v
MPEG-TS / H.264 / AC3
      |
      v
Broadcom decoder -> HDMI
```

No helper PC is required at runtime.

This repository is primarily the writeup and project record for the reverse-engineering work.