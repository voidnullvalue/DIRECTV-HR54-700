# Local UI gate investigation

Live observation on 2026-09-27 shows the UI is not a native `dvr_core` screen: PID 3680 is `/opt/vm/siege ... com.ucentric.core.CoreRunner`, PID 3632 is `dtvwm`, and PID 4792 is `dvr_core --recordingLibrary /var/viewer`.

Important correction: in this build the literal **755** maps to `S6259`: “Server Issue: Card Identification problem.” The satellite-dish error family is 771/775/776. Therefore the visible reported "dish / 755" state must be captured by its exact on-screen text or OSD ID before a patch; it is unsafe to conflate a card-ID OSD with a satellite-loss OSD.

No vendor file, MTD partition, registry value, or process was modified in this phase.
