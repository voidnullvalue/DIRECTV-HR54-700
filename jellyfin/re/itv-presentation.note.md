# ITV presentation trace, 2026-09-27

The host serves `/tv/` and the receiver WebKit loads it, but Druid remains on
LiveTV screen 2320. `itvGetAppStatus` reported session 0 running app ID 7 and
the four `/opt/itv/itvpack/root/bin/itv -i {0..3}` workers were alive. The TV
still showed the stock no-satellite surface in the user's physical check.

The host's `Receiver.launch_itv()` still called `itvEnableScreen` after
`itvStartApp`. Receiver logs show that call led to
`WebViewPrivate::enableScreen`, `AppMgrNotifyAppStopped`, and a load of
`about:blank` on multiple direct launches. That call has been removed. Direct
launch remains a page-loading probe, not a proven visible-app path.

`DruidTester command="setDruidProperty"` requires `name` and `value` XML
attributes. A disposable `hr54_probe_unused=1` property returned
`Property hr54_probe_unused=1`; `removeDruidProperty` with the same `name`
returned `Property hr54_probe_unused was removed`. This establishes syntax but
does not establish an ITV ownership property. No production property was set.

The `90712` string adjacent to `MiscConfigPanelRemoteItv` in `dtv.car` was
tested as a possible screen ID. `gotoScreenById 90712` returned `Done`, but
Druid logged `nextScreen is null` and `SCREEN_CHANGE_ABORTED`; current screen
stayed 2320. It is not a usable shortcut to the panel.

The signage trace still fails immediately after
`DigitalSignageService.launchApp` announces its attempt. Stack:
`launchApp bco=23` -> `BbAppService.itvDigitalSignageStart bco=37` ->
`ItvService.itvDigitalSignageStart bco=23`. The `dtv.car` string pool for
`DigitalSignageService` contains `bbAppService` and `itvFeatureStarter`, but
this does not identify which reference is null. A method-level CEEJ decode or
equivalent runtime trace is needed before changing that path.

The DTV window manager library exports `DTVWM_enablePlanes` and
`DTVWM_getEnabledPlanes`, and the receiver logs show WebKit connecting to
`dtvwm`. No plane manipulation was attempted: the missing transition should
first be compared with a normal Druid-owned `TvAppService` launch.

Next target: find an existing Druid action that constructs a
`BroadbandAppRequest` for the local URL, then compare `AppService`/
`IporAppMgr` state and window-manager plane activity against direct launch.
Do not treat `LoadCompleted` or `itvGetAppStatus=Running` as TV visibility.
