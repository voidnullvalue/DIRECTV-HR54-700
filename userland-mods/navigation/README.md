# Live navigation findings (2026-09-27)

Raw receiver output is in `live-state/`. The receiver clock was in August 2024; host collection date was 2026-09-27. Process IDs are boot-specific.

The exact blocking dish display was OSD **36-775** over base `LiveTVScreen` (screen ID **2320**). The registry files `/var/mw_registry/Registry/Device/Server/OSD/Current/{Number,Extension}` held `"36"` and `775`. Archived `messages.log.100.gz` confirms Druid showed `36-775` and the 775 diagnostic flow entered troubleshoot state `10000`. Its local test path was `DispatchTunerControl.swmDataFeedTest` → `swm_not_detected` → `SignalManagerImpl NOT_DETECTED` → `Aggregate775TestResult SWM_DOWN_INTERNAL_CABLE` → Druid OSD 36-775. This is SWM dish hardware detection, not literal error 755.

Read-only Druid screen query:

```sh
/opt/middleware_core/system/tv/uconntest '<com.directv.druid.dt.DruidTester command="getCurrentScreenId" session="local"/>'
```

`dt removeOsd -osd 36 -session 0` cleared the dish overlay. It did not reappear in the observed checks. `dt simSignalLock -tunerNum 0 -session local` did not clear it; a tuner lock event alone is not sufficient for the SWM diagnostic state. After removal, local SHEF `/remote/processKey?key=menu&hold=keyPress` opened Menu (screen ID **10306**) without an OSD. SHEF key names are lowercase on this build.

`key=list` staged screen ID **1900**, then Druid aborted and showed OSD **117**, with text `DVR service is not activated on your account`. Direct `DruidTester gotoScreenById screenId="1900" session="local"` returned `Done` but hit the same gate, with `SharedAlgorithms.isDvrActionAllowed` in the logged call stack. The receiver reports `not paired, not authorized, not DvrSubscriber` from `dt getCardStatus`. These are observations only; no subscriber state was changed.

The `OsdListeners` diagnostic entry `OSD 1000036` is listener registration and by itself does not prove on-screen display. The registry plus Druid boot log establish the displayed OSD here.
