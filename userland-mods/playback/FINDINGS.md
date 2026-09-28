# DVR playback refusal — findings

Date: 2026-09-27 (live receiver 2024-08-23 clock)
Question: can an existing **local** HR54 recording be pushed to the stock playback
stack without loading the subscriber-gated playlist screen (1900)?

## Answer (definitive, per live evidence)

**No — not through the stock DVR playback layer as it stands.** The refusal is a
genuine **DVR subscription-entitlement gate**, not a UI-navigation artifact. It is
enforced at the DVR-playback *initiation* point in Druid/MediaPlayer, so every
entry point we could reach is refused identically.

## The gate

- Druid method `com.directv.druid.util.SharedAlgorithms.isDvrActionAllowed` (in
  `dtv.car`) gates DVR actions. Its neighbourhood in the constant pool includes
  `isDvr`, `isDVRNormally`, `isMRVExposed`, `getActiveAndObservedHoldersNumber`,
  and the entitlement cluster `isDvrSubscribed`, `getDvrSubscriber`,
  `setDvrSubscriber`, `mDvrSubscriber`, `DvrSubscribed`,
  `isDvrSubscriptionRequired`, `DvrSubscriptionRequired`,
  `HddWithoutDvrServiceOsd`.
- When the gate is false, Druid raises **OSD 117**: `DVR service is not activated
  on your account. To activate, call 1-800-XXX-XXXX.`
- The gate reads the access-card subscription state. `dt getCardStatus` →
  `Card: not paired, not authorized, not DvrSubscriber, status=0, type=0, ID=-11`.
  With `DvrSubscriber == false`, `isDvrActionAllowed` is false and playback is refused.

## Three entry points, all refused (OSD 117)

1. **UI key path** — `GET /remote/processKey?key=list` (and the `list` menu item)
   attempts screen 1900 and aborts with OSD 117.
2. **Forced screen navigation** —
   `DruidTester command="gotoScreenById" screenId="1900"` returns `Done` and
   stages `nextScreenId:1900`, but the PlayListScreen load itself raises OSD 117.
   Verified afterward: `DruidTester command="getCurrentScreenId"` still reports
   **2320** (LiveTVScreen) — the transition was rejected, not merely deferred.
3. **Direct AGPL playback** (bypasses the UI entirely) —
   `DruidTester command="aplPlayback" index="1"` (index 1..N of the aggregated
   "My Recordings" playlist) also raises **OSD 117** and does not start playback.

Because (3) is a direct, non-UI call into the AGPL/playback path and is still
gated, the check is **not** confined to the UI layer. This is the key finding:
the subscriber check *is* the playback-engine gate.

## Supporting surface discovered (non-mutating)

- `DruidTester` (via `uconntest`) exposes: `gotoScreenById`, `getCurrentScreenId`,
  `listLocalUI`, `aplPlayback`, `getRUISessionIds`, `setDruidProperty`, etc.
- `pvruconnect.DirectTest` command surface (probed with a bad-index oracle; a
  recognized command emits a specific error, an unknown one returns `done`):
  - Real: `watch` (live tune), `schedule`, `manualRecord`, `recurringManualRecord`,
    `librarySetDeletion`, `scheduleSetDeletion`, `stopRecording`, `playViivVideo`
    (ViiV), `playURL`.
  - The `no command (try schedule|cancel|library|play(index))` usage string is
    **stale** — `cancel`, `library`, `play` are NOT current command names (they
    fall through to `done`).
  - No one-shot "play DVR item by index" command exists at this layer.
- SHEF `/dvr/*` endpoints: `/dvr/play` 401 (no params) / 403 `Invalid URL
  parameter(s) found` (e.g. `?uniqueId=…` — wrong param); `/dvr/playPrivate` 404
  `Resource not found.`; `/dvr/trickPlayPrivate` and
  `/dvr/getRecordingInfoPrivate` 403 `Command not allowed.`

## Why the stock path is refused (classification)

This is **category A — subscriber/entitlement gate** (backing onto the access
card), not:
- B (a benign local DVR feature flag that can be flipped), or
- a purely cosmetic UI gate.

The DVR media pipeline itself is alive (`dvr_core` PID 4792, `MediaPlayer(S0-P0)`,
`MediaPlayerProxy`, AGPL local DVR connection present), and the recording library
is readable. What is refused is **starting playback of a recording**, because the
receiver is not a `DvrSubscriber`.

## Superseded: the physical subscriber card

The user obtained the subscriber card that shipped with the receiver and inserted
it. This is the correct fix and **it makes every spoofing/patching option
moot**. Current state:

- `dt getCardStatus` improved from `status=0, type=0, ID=-11` (no card) to
  `status=1, type=0, ID=CARD_ID_REDACTED` (`CARD_ID_REDACTED`) — the card is now detected.
- It is still `not paired, not authorized, not DvrSubscriber`, so OSD 117 remains
  and `startDtcp()` still fails with `AccessCard not present or not paired`.
- A full reboot (user-approved) did not complete pairing, so the failure is not a
  hot-insert artifact.
- **`dmesg` shows a smartcard read error**: `!!!Error (0x9) at
  NEXUS_Smartcard_Read_impl:521` with `ERR cdi_smartcard line 2573` / `line 1412`,
  against `/dev/bmoca0` (char 234,0). The box is not able to actually read the
  card, so this is a card-reader/contact/seating problem rather than a
  software entitlement problem.

The binary-patch plan (`SharedAlgorithms.isDvrActionAllowed`, frame
`bco=73`, which raises OSD 117 directly) was **never executed**;
`/opt/dtv/dtv.car` md5 remains `882124071cfe3ac1740af18161995dcf`. It is only
worth revisiting if the card cannot be made readable and authorized.

## Earlier levers, all exhausted (retained for the record)

### Method A: CA device-authorization injection

The user authorized a reversible CA/device-authorization injection as the chosen
approach. It was implemented and **it does not reach the DVR subscription gate.**

Entry points confirmed on the live receiver:

- `pvruconnect.DirectTest command="setDeviceAuths" file="<path>"` — the **device**
  (FCT / receive-authorization) path. Correct attribute name is `file`; proven by
  `File <path>does not exist!!!` for a bogus path. The file is genuinely read.
- `pvruconnect.DirectTest command="submitAuthRequest" file="<path>"` — the **client**
  (RUI) path, which is a *different* subsystem; it rejected our device message with
  `ERROR: Unknown type of authorization request has been submitted.`

Parser structure recovered from `dtv.car` (class
`com.directv.mw_core.deviceauthorization.test.DeviceAuthCapTest`):

```
MESSAGE_START
DEVICE_AUTHORIZATION <caps fields>
DEVICE_AUTHORIZATION_DEVICE <device fields>
MESSAGE_END
```

Field-name pool: `caps`, `number_of_devices`, `sequence_flag`, `sequence_number`,
`stb_auth_mode`, `stb_auth_lifetime`, `stb_auth_expire_flag`, `cal`, `byteCount`,
`reserved1`..`reserved4`. Parser diagnostics: `Number of fields in device
authorization line is less than expected`, `Values for total and sequence_number
are missing`, `Unique ID is expected as a hexadecimal string.`, `Wrong length of
hexadecimal string!`, `Wrong alphanumeric value:`.

The caps object model is `com/directv/mw/capdspchr/DeviceAuthCap` →
`DevAuthCapVO(timestamp:, state:, seqNum:, seqTot:, lifetime:, auths{…})` with
states `FCT_STATE`, `GENERIC_STATE`, `LENIENT_STATE`, `NON_STATE`, plus
`clientTrackingState`.

**Every** input — bare markers, 1..20 fields, varied `total`/`sequence_number`
ordering, 8- and 16-hex-digit UIDs, a `DEVICE-AUTH-CAP::` cap payload — produced
byte-identical output:

```
Generated caps:
[FCT]
java.lang.Exception:   (empty message)
    at …DeviceAuthCapTest.readTestFile(bco=72)
```

The failure is at a fixed point, independent of message content, and the cap set
comes back empty (`[FCT] ` prints with an empty payload). The test entry point
appears non-functional in this firmware build, and the format is not recoverable
from the binary (no template or example exists on the receiver).

More fundamentally, the subsystem is the **wrong target**. `DeviceAuth` /
`DeviceAuthAnnouncementProcessor` / `DevAuthCapVO` manage the STB's FCT /
receive-authorization and client-tracking state. The playback gate reads
`isDvrSubscribed` ← `DvrSubscriber`, which is set from the **access card** by
`com/ucentric/discovery/AccessCard` / DiscoveryManager. These are separate state
variables; a successful FCT injection would still leave `DvrSubscriber == false`
and OSD 117 in place.

**Conclusion: method A is a dead end.** It was the last entitlement-side lever
available without patching a vendor binary.

## Other levers tested and reverted (all reversible probes)

- `setServiceLevel num="6"` — set `ClientManager Service Level` to 6 (override);
  OSD 117 still raised, so it is not a gate input. **Restored to 0.** Note there
  is no working `clear` form: `clear=""`, `clear="true"`, `clear="clear"`,
  `clear="1"`, `clear="yes"`, `clear="on"` all return the usage line, so the value
  stays flagged "set by override".
- `setAuthorization hd="true" atsc="true"` — did **not** change `getCardStatus`
  (still `not paired, not authorized, not DvrSubscriber`). **Reverted**;
  `getAuthorization` now reports `atsc = false, hd = false`.

No account, entitlement, card, subscription, or `DvrSubscriber` state was actually
changed — the card reports identically to baseline after every probe. No flash,
kernel, root, or vendor file was modified.

## State left behind (verified clean)

- `getCardStatus` → `not paired, not authorized, not DvrSubscriber` (baseline).
- `getServiceLevel` → `ClientManager Service Level = 0` (pre-test value).
- `getAuthorization` → `atsc = false, hd = false` (baseline).
- `getCurrentScreenId` → 2320 (LiveTV); OSD 117 removed via
  `dt removeOsd -osd 117 -session 0`.
- All probe files deleted from `/var/opt/hr54/playback/`.

## Next step

The blocker is now hardware, not policy or code: the receiver cannot read the
subscriber card (`NEXUS_Smartcard_Read_impl:521` against `/dev/bmoca0`), so it
stays `not paired / not authorized / not DvrSubscriber`.

1. **Re-seat the card** — verify orientation, full insertion, and clean/docked
   contacts in the slot, then re-check `dt getCardStatus`.
2. If the card then reads but stays unpaired, it needs **DIRECTV account
   activation for this `receiverID` (RECEIVER_ID_REDACTED)** — an external action, not a
   software change.
3. Only if both fail, revisit the `dtv.car` gate patch (force
   `SharedAlgorithms.isDvrActionAllowed` true at the method whose frame reports
   `bco=73`), or fall back to decoding the stored recording payload directly
   below the entitlement layer.

Note that even past the gate, content protection may still refuse playback:
`getDvrCci`, `getIsNoPECMKey`, `getIsNotEntitled` are present, and DTCP cannot
start while the card is unread.
