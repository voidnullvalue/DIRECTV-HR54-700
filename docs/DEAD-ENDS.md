# Dead ends and traps

Everything here was tried, cost real time, and **did not work**. A reproducer
should not repeat any of it. Each entry says what was attempted, why it fails,
and what to do instead.

---

## 1. kexec → `ENOSYS`

**Attempted:** boot a replacement kernel via `kexec_load()`.

**Result:** `ENOSYS` (errno 38), returned immediately. The syscall is not
implemented in this kernel — it is not a permission problem and not a signature
problem. `linux-port/kexec/build-kexec-probe.py` is the probe: an 8-instruction
MIPS ELF that calls `kexec_load(0, 0, NULL, 0)` and **exits with the raw
return value**, so `EINVAL` (22) — implemented, bad request — is
distinguishable from `ENOSYS` (38) — not implemented. That request cannot load
an image and changes no kernel state.

**Instead:** the plugin-image path. `/var` on the disk is writable, and the
boot-time `indexer` probe gives root. See [ROOT.md](ROOT.md).

**Cost of getting this wrong:** you can burn a lot of time believing kexec is
gated by verification. It is not implemented. Full stop.

---

## 2. DVR entitlement / OSD 117

**Attempted:** make the stock DVR playback path work.

**Result:** the gate is a *subscription entitlement*, not a technical
capability, and it is closed. Proven through **three independent entry points**,
all of which refuse:

1. the UI key path,
2. a forced `gotoScreenById`,
3. a direct `aplPlayback` call.

The check is `SharedAlgorithms.isDvrActionAllowed` → `DvrSubscriber`. Even
after a subscriber card is inserted and electrically detected (`status=1`), the
receiver stays `not paired, not authorized, not DvrSubscriber`, and a reboot
does not complete pairing. DTCP still fails with `AccessCard not present or
not paired`. `dmesg` shows `NEXUS_Smartcard_Read_impl:521` against
`/dev/bmoca0` — a **card-reader failure**, not a software gate.

**Why no software fix exists:** the refusal comes from the card and the
account. It is enforced by entitlement, not by code you can patch around, and
patching the check would be circumventing access control for paid content. Out
of scope, deliberately.

**Instead:** abandon the DVR path. Build a media path that never consults
DIRECTV's entitlement system — your own content, your own server, the vendor's
own decoder. See [PLAYBACK.md](PLAYBACK.md).

**Trap:** the access-card ID and receiver account ID recorded during this
investigation are **redacted** from this repository. Nothing in the tooling
needs them. See [NOT_INCLUDED.md](NOT_INCLUDED.md).

---

## 3. `playURL` creates no `VODCapture`

**Attempted:** control playback with DIRECTV's native DVR transport API —
call pause and stop like DVR pause and stop.

**Result:** `playURL` **does not create a `VODCapture` object.** The transport
controls operate on `VODCapture`. There is nothing for them to act on.

**The nastiest part:** pause and stop SHEF requests **return success** while
playback continues unaffected. A success return code here does not mean the
framebuffer stopped. `jellyfin/tools/hr54-stopprobe.sh` exists because
discovering *which* command actually does anything took brute force.

**Instead:** control the data path. `playURL` is just an HTTP client pulling
bytes, so `hr54-jf` gates the `/play/<token>.ts` response — stop sending bytes
while keeping the socket open (pause), continue (resume), close (stop), or
re-request at `StartTimeTicks` (seek). See
[PLAYBACK.md](PLAYBACK.md#transport-pause-resume-seek-stop).

**Trap:** if you close the socket to "pause", the receiver tears down the
player and drops to the menu — indistinguishable from stop. Hold the connection
open and withhold bytes.

---

## 4. A new arbitrary Settings row is whitelisted out

**Attempted:** add an eighth "Jellyfin" section to the stock Settings menu by
overlaying the `STB_Settings` FlatBuffer at boot.

**Result:** the **overlay mechanism works** — the receiver picks up an overlaid
resource from `/proc/mounts` at boot, verified with a live capture. But the
**compiled `createMenuRowMap` hardcodes seven stock section refs.** An eighth,
arbitrary row is filtered out and never renders, no matter what the FlatBuffer
says.

**Therefore:** the FlatBuffer resource can never be sufficient on its own. Do
not spend more effort on layout resources. This was measured, not assumed.

**Related trap:** a working `SELECT` action for such a row cannot exist without
a code change, and the only code-change candidate (`/opt/dtv/dtv.car`) is a
**CEEJ VM bytecode archive**, not native code. Patching it means patching
bytecode, which risks crashing `siege`. Judged not worth it — and **never
executed**.

**Instead:** the MENU watcher + fullscreen ITV launch. See
[ITV-WEBKIT.md](ITV-WEBKIT.md).

The FlatBuffer tooling is still published, because it is good work and the
*overlay mechanism* finding is what made the menu watcher possible:
`jellyfin/ui/inspect_settings.py`, `add_settings_row.py`, `rename_help_tile.py`.

---

## 5. DirectTest ITV start vs Druid ownership

**Attempted:** launch the vendor WebKit/ITV renderer against a local URL.

**Result — a genuine deadlock, two halves:**

| Route | Behaviour |
| --- | --- |
| `DirectTest` start | the app **starts and successfully fetches our page over the LAN** — but Druid never transfers presentation ownership. Nothing appears. Screen ID never changes. |
| Druid property / screen ID | Druid only accepts properties from a context that **already owns** the screen, and nothing in our process can be given that ownership. |

The renderer works. It loads HTML from your LAN. It just cannot be made to
*display* it.

Also ruled out along the way: `itvEnableScreen` (removed from this build),
`setDruidProperty` (accepted, ignored), screen id `90712` (no present),
`DigitalSignageService.launchApp` (failed with a stack trace).

**Instead:** the MENU watcher. Wait for the user to press MENU, which makes
Druid yield the screen, then win the race. Fullscreen, no chrome.

**Keep** `jellyfin/ui/itv-test.html` — the fastest way to retest whether a
future firmware build fixes the Druid ownership bug.

---

## 6. Screensaver: wrong-resource discovery

**Attempted:** replace the boot splash.

**Trap:** there is more than one candidate path, and the obvious one is wrong.
The live file is:

```
/opt/ui_assets/assetspack/images/screensaver.png
```

`images/alt/screensaver.png` is a **symlink** to `../screensaver.png`, so a
mount over the wrong path appears to work and changes nothing. The stock PNG is
also **8-bit colormapped**, while ours is RGBA — a useful authorship check.

**Instead:** bind-mount over the real path, and `umount` on rollback. See
[ITV-WEBKIT.md](ITV-WEBKIT.md#screensaver) and
`jellyfin/ui/rollback-local-www-and-screensaver.sh` — the `umount` line is
load-bearing, and without it the stock art stays hidden behind our PNG.

---

## 7. `-mcpu=mips32r2` → Illegal Instruction

**Attempted:** cross-compile `hr54-jf` for `mips-linux-musleabi` with the
toolchain default.

**Result:** modern toolchains default to a newer ISA revision. An
`mips32r2` build **compiles, links, and runs perfectly under a host emulator —
then dies with `Illegal Instruction` on the real receiver.** The SoC is MIPS32
version 1.

**This is the single most expensive trap in the project**, because every local
test passes. The host test suite builds for the host architecture, so it cannot
catch this. You find out on hardware.

**Fix:**

```sh
zig cc -target mips-linux-musleabi -mcpu=mips32 -static -O2 -o hr54-jf hr54_jf.c
```

**Always pin the ISA.** Any `mips-linux-musleabi` toolchain is fine, as long as
`-mcpu=mips32` is explicit. See
[HARDWARE.md](HARDWARE.md#build-for-mips32-never-mips32r2).

---

## 8. The `.25` host backend prototype

**Attempted:** run the media proxy and TV UI on a development host, with the
receiver only acting as a dumb decoder.

**Result:** it worked — this is how the first end-to-end playback was
achieved. But it is a bad appliance. It means the TV experience depends on a
second machine, and the token/URL handling spans a network hop.

**Kept, clearly labelled:** `jellyfin/remote/server.py` and `jellyfin/client/`
are the host prototype. They also drive the host test harness, and
`jellyfin/client/auth.py` is the clean reference for the Quick Connect
contract.

**Retired from production:** port **8131** is gone, its firewall rule removed.
**8130** is the production port and is the receiver-native backend.

**Rule:** the final appliance must not reference `the development host` or port `8131`
anywhere in production scripts. Historical mentions are confined to
documentation that says so. Every production script here uses a placeholder or
a required environment variable, and a test would fail if one crept back in.

---

## 9. Dirty COW (CVE-2016-5195)

**Attempted:** Dirty COW for a local root stage.

**Result:** the vulnerability **is confirmed present** — BuildID, exact
`__get_user_pages` / `get_user_pages` addresses, and a disassembly-level
argument that `FOLL_WRITE` is not cleared after copy — but it is now
**completely unnecessary**. We already have root from the plugin image. It is
documented, not used.

**Do not spend time here.** The plugin path is simpler, more reliable, and
survives reboot.

Other candidates (pppd 2.4.4, BusyBox `udhcp`, uClibc 0.9.32.1) are
**version-range matches only**, with no config or reachability evidence. They
are not marked exploitable and should not be treated as such.

---

## 10. BIST / GGUP / `dtv.car` as an attack surface

- **BIST USB update** verifies signatures in hardware. Not a route.
- **`.flash.ec`** is a signed update container. Not a route.
- **The exposed RSA key at `/opt/ggup/bin/DTV_STB_BASE_TEST.key`** is a
  *complete, unencrypted* 2048-bit key — and it is a **GGUP test-TLS
  identity, not a plugin-signing root**. `wolfSslServerCtxNew` is how the
  analysis reaches that conclusion. It cannot forge a production plugin.
  **BLOCKED as a shortcut.** The key is not published here.
- **`dtv.car`** is a CEEJ VM bytecode archive. Bytecode patching risks crashing
  `siege`. Never attempted.

**Conclusion:** MTD/flash is untouched, and the trust boundary stays intact.
The disk-side plugin path is the intended surface and it is sufficient.

---

## 11. Smaller traps

| Trap | Reality |
| --- | --- |
| **`/var/opt` is cleaned every boot** | a naive install vanishes. Use `/var/hr54-persist`. [PERSISTENCE.md](PERSISTENCE.md) |
| **`NVGetObject` is gated** | returns code 3 (read denied) / 12 (semaphore). A naive id sweep of `100..65535` returns 5 uniformly and teaches you nothing. Use the raw EEPROM primitives. [WIFI.md](WIFI.md) |
| **`NVReadBytesFromEEPROM` argument order** | passing the buffer as the 2nd argument returns 0 without writing — looks exactly like an empty device |
| **Passphrase is at `+0x21`, not `+0x20`** | off-by-one yields an image that cannot associate. The receiver reads 65 bytes from the wrong start |
| **A statically linked musl binary cannot `dlopen()`** | "Dynamic loading not supported". Use an `LD_PRELOAD` shim; the box's own `ld-uClibc` resolves the symbols |
| **The vendor library has no SONAME** | `build.sh` rewrites `DT_NEEDED` to the bare filename, or it will not resolve |
| **The receiver clock may be wrong** | one session read August 2024. Do not trust timestamps on the box for evidence; use the host clock |
| **You cannot see the TV** | there is no capture path. Verify on the physical screen, or you have not verified it |
| **`dt simSignalLock` does not clear OSD 36-775** | the real failure is SWM detection (`swm_not_detected`); a tuner-lock event does not change it |
| **Literal 755 ≠ satellite error** | in this build `755` is `S6259` "Card Identification problem". The dish family is **771/775/776**. Do not conflate a card-ID OSD with a satellite-loss OSD |
| **A shared library is the wrong target** | the trigger is a boot probe that **executes `/opt/mp4lib/bin/indexer`**, not `Mp4libFeatureStarter` loading `libmp4lib.so`. The first attempt was wrong about this |

---

## How to use this page

If you are about to do something on this list, stop. If something is *not* on
this list and it fails, that is genuinely new information — please write it up.
