# Proven vendor media path

LAN HTTP → `DirectTest.playURL` → `MediaPlayer(S0-P0).watchRemoteStream` → `MediaPlayerProxy.load` → `dvr_core` LocalPlayer → `libdvr::RemoteMediaStream` → MPEG-TS demux → CDI/Broadcom audio/video decode → HDMI.

The observed stream had video PID 256 (H.264/Mpeg4Pes) and audio PID 257 (AC3). Logs reported `TRANSPORT_STREAM_MPEG`, `MediaPlayer.notifyStarted`, `notifyFramePresented`, `VIDEO ENABLED`, `AUDIO ENABLED`, and position updates. Physical confirmation established both picture and sound. `dvr_core` hosts the generic player; this does not make DVR recording entitlements a prerequisite.
