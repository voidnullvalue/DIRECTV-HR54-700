# Established call/navigation path

## Satellite-loss path (class-level, not yet method-decompiled)

`AMS SignalLossEvent` → `handleSTBSignalLossEvent` / `validateSignalLossEvents` in the Siege/Druid code → `SystemEventMgr` / `OsdManager` → signal-loss OSD.

The live listener dump confirms the active local base screen is `LiveTVScreen`, while an OSD is registered. Thus the error presentation is likely an OSD/modal layer, not the DVR playback service and not proof that the current `DtvScreen` is a 755 screen.

## Recordings path

`ScreenHistoryMgr.gotoScreen(...)` → playlist factory (`PlayListScreen` / `aggregatedPlaylist.PlayListTopHighlightScreen`) → `AGPLManager` → `aplGetRecording` / `playItem` → `TrickPlayMgr` / `VideoMgr` → stock recording service (`dvr_core`) → NEXUS-backed decode.

The live `RecordingDT library -detail` query returned 261 stock records while no satellite was present. This proves the local recording-library path is alive independently of acquisition; playback has not yet been invoked.
