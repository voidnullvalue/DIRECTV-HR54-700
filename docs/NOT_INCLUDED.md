# Not included

Complete, exhaustive list of material from the original working tree that is
**not** in this repository, with the reason and how to regenerate or obtain it.
Nothing here is a surprise; if something you need is missing, it is on this
page.

Classifications used: **secret** (contains a credential), **proprietary**
(copyrighted vendor content), **enormous** (impractical to host), **generated**
(derived output), **junk** (superseded experiment with no forward value).

---

## A. Proprietary vendor content — obtain from your own receiver

| Working-tree path | Class | Why excluded | How to obtain | Consumed by |
| --- | --- | --- | --- | --- |
| `corpus/plugins/7_6932_6932.squashfs` | proprietary | genuine signed DIRECTV plugin image | `hr54-pull.sh` from `/var/network/plugins/7/` | `asset7/build-plugin-v2.sh`, `build-plugin-v3.sh` |
| `corpus/plugins/7_6932_6932.sig` | proprietary | genuine signature; **reused verbatim, never forged** | same directory | same |
| `corpus/plugins/*.squashfs` (all ~20) | proprietary | genuine update plugin images | receiver `/var/network/plugins/` | `tools/csw_inspect.py` |
| `corpus/csw/image_mfr-*_tar-ffff.csw` (12–17 MB each) | proprietary | Pace C41/C61K client firmware CSWs | receiver TFTP cache / update corpus | `tools/csw_inspect.py` |
| `extracted/sdb4-rootfs/` (whole tree) | proprietary | full recovered vendor rootfs: `dtv.car` 20 MB, `libdvr.so` 12 MB, `libcwebkit.so` 14 MB, `europa.ko` 13 MB, `libapg.so` 5 MB, … | `hr54-pull.sh` / `tftpget.py` | `asset7/verify-offline*.sh`, `wifi/build.sh`, analysis only |
| `extracted/sdb4-rootfs/opt/sig/bin/sigtst` | proprietary | the vendor signature verifier — we *run* it, never modify it | `hr54-pull.sh /opt/sig/bin/sigtst` | `asset7/verify-offline*.sh` |
| `extracted/sdb4-rootfs/opt/nvram/lib/libDtvNVRamMgr.so` | proprietary | NVRAM manager; the RE target | `hr54-pull.sh /opt/nvram/lib/` | `wifi/build.sh`, `build-write.sh` |
| `poc-hardware/beachhead-plugin/root/**/*.so`, `*.bin` | proprietary | genuine Broadcom `libmp4lib.so`, `libavcodec.so.57`, `libdash.so`, `libabr.so`, `bcm7346_aud{dec,enc}_aac.bin`, … re-packed unchanged | come from the genuine image at unsquash time | `asset7/build-plugin-v3.sh` |
| `poc-hardware/beachhead-plugin/root/mp4lib/bin/indexer.real` | proprietary | **the genuine vendor `indexer`** — preserved and re-`exec`'d | comes from the genuine image | `asset7/build-plugin-v3.sh`, `verify-offline-v3.sh` |
| `jellyfin/ui/backup/asset7-before-jellyfin.squashfs` | proprietary | full stock vendor plugin image | receiver | `jellyfin/ui/rollback-local-www-and-screensaver.sh` |
| `jellyfin/ui/build/asset7*/` (extracted trees, 7.7 MB each) | proprietary | extracted vendor trees | `unsquashfs` the genuine image | build scripts |
| `jellyfin/ui/build/asset7-jellyfin-v*.squashfs` (v1–v5) | proprietary | built images containing vendor `.so`s | `asset7/build-plugin-v3.sh` + `jellyfin/ui/activate-local-www-and-screensaver.sh` | deploy step |
| stock UI art, `screensaver.png` | proprietary | DIRECTV first-party artwork | receiver `/opt/ui_assets/assetspack/images/` | bind-mounted over, never redistributed |
| `STB_Settings.*.fb` (stock) | proprietary | vendor UI resource | receiver | `jellyfin/ui/inspect_settings.py` |
| `test-key-private.der`, `test-key-public.{der,pem}` | proprietary-adjacent | recovered from `/opt/ggup/bin/DTV_STB_BASE_TEST.key` on the device | receiver | analysis only. **The key itself is not published.** It is a GGUP *test-TLS* identity, **not** a plugin-signing root — see [ROOT.md](ROOT.md). |

---

## B. Disk images and dumps

| Working-tree path | Size | Class | Why excluded | How to regenerate | Consumed by |
| --- | --- | --- | --- | --- | --- |
| `sdb3-rt-placeholder.img` | 983,406,247,936 B (~916 GiB, **sparse**) | enormous | absurd on purpose: an all-zero file that satisfies XFS geometry so partition 2 can be mounted without touching sdb3 | `truncate -s 983406247936 sdb3-rt-placeholder.img` | `asset7/install-*.sh` |
| `raw/sdb2.img` | 16,113,320,448 B (15 GiB) | enormous, verbatim owner data | a byte-for-byte copy of the owner's disk | `ddrescue /dev/sdX2 sdb2.img sdb2.map` — SHA-256 `1b93be57da0aa5a07eb54bbf900a3bb6d778e0c1a82aee4472223e304737c0ad` | `asset7/restore-original-sdb2.sh` |
| `raw/sdb1.img` | 542,835,712 B | enormous, proprietary | never written by this project | `ddrescue` if you need it | — |
| `raw/sdb3.img` | — | enormous | ditto | — | — |
| `raw/sdb4.img` | 139,829,760 B | enormous, proprietary | ditto | — | — |
| `raw/sdb2.map` | — | generated | ddrescue map, derived | `ddrescue -m` | `restore-original-sdb2.sh` |
| `samples/sdb3-first-512MiB.img`, `sdb3-last-512MiB.img` | 512 MiB each | enormous | heuristic sampling | `dd` with offsets | discovery only |
| `tmp/inspect/sdb2-copy.img` | 5,102,100,480 B | enormous, generated | scratch copy for `xfs_repair` | make your own copy | analysis only |
| `poc-hardware/mips-vm/sdb2-test-overlay.qcow2` | 5,505,024 B | generated | QEMU test overlay | `qemu-img create -f qcow2` | `asset7/install-via-mips-vm.sh` |
| `poc-hardware/mips-vm/vmlinux-4.19.0-21-4kc-malta` (14 MB) | — | generated | Debian `linux-image-mips` kernel, not DIRECTV | `apt-get install linux-image-mips` | QEMU guest |
| `poc-hardware/mips-vm/*.deb` (35 MB), `linux-image/`, `initrd*.gz`, `*-initrd.gz` (25 MB each ×9) | — | enormous, third-party | Debian kernel/initrds | `poc-hardware/mips-vm/build-initrd.sh` | QEMU guest |
| `pvr/live-20260927/*.tgz` (66 + 66 + 42 MB), `pvr/live-20260927/extracted/` (169 MB `REG.log`) | — | enormous, proprietary | verbatim `/var/viewer` dump of the owner's receiver | `hr54-pull.sh /var/viewer` | `pvr/tools/pvr_catalog.py` |
| `poc-hardware/mnt-sdb2/`, `poc-hardware/preflight-mnt/`, `mounts/` | — | proprietary, generated | mount points over live device images | — | — |
| `kernel/kernel.bin` | 5,745,024 B | proprietary | extracted flash kernel | dump the MTD on your own | analysis only |
| `inventory-elf.tsv`, `inventory-crypto.txt`, `root.hash` | — | generated / **secret** | derived; `root.hash` is a real credential hash | regenerate with `tools/elf_inventory.sh` | — |

---

## C. Secrets — redacted or excluded

| Item | Working-tree path | Class | Action taken |
| --- | --- | --- | --- |
| **sudo password** for a local user, hardcoded in 6 scripts | `poc-hardware/{check-sdb2-diag,check-sdb2-evidence,check-sdb2-readonly,install-v2-via-mips-vm,install-v3-via-mips-vm,recover-sdb2}.sh` | secret | **removed.** Now `HR54_SUDO_PASS` env or `~/.hr54-sudo-pass` (mode 600), and the `su` privilege hop is gone |
| **receiver `root` password hash** | `tools/crack_root_hash.pl` | secret | **removed.** Hash is now an argument / `HASH=` env / `--hash-file`, never embedded |
| same | `root.hash` | secret | **excluded entirely** |
| same | `REPORT.md` §7 | secret | **redacted** in `docs/ORIGINAL-REPORT.md` |
| **WiFi SSID** of the original author | `wifi/nvwrite.c` (2 assertions) | secret | **removed.** Replaced with a structural credential check (`cred_ok()`) that validates the fields without embedding any value. A first-passphrase-character literal was also removed |
| **WiFi passphrase** | `wifi/nvwrite.c` docstring length hints | secret | generalised; the field is now described structurally |
| **NVRAM images** (original + patched) | staging area on the device | secret | **excluded.** Never publish these — they contain a credential. Regenerate with `dd if=/dev/nds/nvram0` |
| **Jellyfin access token** | runtime only, `config/token` | secret | **never in the repo.** `hr54-jf` persists it mode `0600`; the test suite asserts logout deletes the file |
| **Quick Connect `Secret` / `AccessToken`** | runtime only | secret | **never in the repo.** Stays in the backend; the TV sees only the 6-char code |
| **Jellyfin server address** (a real LAN IP) | many docs and scripts | personal | **replaced** with `192.0.2.0/24` (RFC 5737 TEST-NET-1) placeholders, or an explicit `JELLYFIN_SERVER` requirement |
| **receiver LAN addresses** (`.101`, `.103`) | many scripts/docs | personal | **replaced** with `192.0.2.10` + a required `HR54_HOST` guard |
| **LAN subnet** | `jellyfin/ui/rollback-local-www-and-screensaver.sh` | personal | **replaced** with `YOUR_LAN_CIDR_PLACEHOLDER` |
| **Ethernet MAC** `<MAC>` | `poc-hardware/STATE.md`, `REPORT.md` | personal | **redacted** |
| **disk serial** `<DISK_SERIAL>` | 7 `asset7/*.sh` | personal | **parameterised** as required `HR54_DISK_SERIAL` |
| **DIRECTV access-card ID** and **receiver account ID** | `userland-mods/STATE.md`, `userland-mods/playback/FINDINGS.md` | personal, account-linked | **redacted** (`CARD_ID_REDACTED`, `RECEIVER_ID_REDACTED`) |
| activation phone number in a quoted OSD string | `userland-mods/playback/FINDINGS.md` | personal | **redacted** (`1-800-XXX-XXXX`) |
| **private RSA key** | `test-key-private.der` (mode 600) | **secret** | **excluded.** Analysis retained in the original report |
| SPKI / certificate fingerprints | `REPORT.md` §7 | not secret, but a targeting aid | **retained** — public-key digests, useful for identification, not confidential |
| GGUP private key *path* | `REPORT.md` §7 | pointer to live key material | **redacted**; the finding and the conclusion (it is a test-TLS identity, not a signing root) are retained |
| `.88.x` home LAN IPs in the **Pages article** | `index.html` | personal | **left intact on purpose.** The pre-existing writeup is preserved verbatim as required; it is the author's own published article |

---

## D. Development-machine paths

| Path | Files | Action |
| --- | --- | --- |
| `/home/void/vroomfondle/dvr/hr54-re` | `tools/acquire_phase1.sh`, `jellyfin/remote/service/{run,ensure-supervisor.sh}` | **removed** → `$HR54_REPO`, `$HR54_EVIDENCE`, `$HR54_STATE`, `$HR54_DISK` |
| `/tmp/opencode/...` | `wifi/build.sh`, `wifi/build-write.sh`, `jellyfin/remote/test/*`, `jellyfin/remote/README.md` | **removed** → required `$ZIG` / `$LIB`, or `${TMPDIR:-/tmp}` / `tempfile.gettempdir()` |
| `/dev/sdb` literals | `tools/acquire_phase1.sh`, `asset7/*.sh` | **removed** → `$HR54_DISK`, and disks resolved by size **and** serial |

---

## E. Retired experiments and dead ends

Not "not included by accident" — deliberately dropped because a reproducer
would waste time on them. The *findings* are preserved in
[DEAD-ENDS.md](DEAD-ENDS.md).

| Path | Why dropped |
| --- | --- |
| `jellyfin/tools/hr54-hdmi.sh`, `hr54-hdmi2.sh` | HDMI/HDCP line of inquiry, abandoned |
| `jellyfin/tools/hr54-experiment.sh` | byte-for-byte the same technique as `hr54-playurl2.sh`, with worse sentinel filtering |
| `jellyfin/tools/hr54-logtail.sh` | superseded by `hr54-pull.sh` |
| `jellyfin/tools/hr54-playurl.sh` | superseded by `hr54-playurl2.sh` (no OSD dismissal, no file output) |
| `wifi/nvtool.c` | `NVGetObject` is gated (code 3 / 12); also an 8 KB stack claim. Kept only as a documented dead end |
| `poc-hardware/malicious-plugin/`, `corrected-plugin/` | v1/v2 build trees superseded by v3. Would have added ~14 MB of genuine Broadcom `.so`s |
| `poc-hardware/{install-v2-via-mips-vm,verify-offline-v2}.sh`, `offline-verification-v2*.txt` | v2-only; v3 (`verify-offline-v3.sh`) supersedes |
| `jellyfin/remote/static/tv.before-tv-ux/` | earlier TV frontend, superseded by `static/tv/` |
| `jellyfin/remote/bin/{hr54-jf,hr54-www,hr54-www-8131}` | **build outputs.** Regenerate: `hr54-jf` from `hr54_jf.c`; `hr54-www` from `hr54_www.c` (legacy, `hr54_www.c` is published) |
| `wifi/{nvpre.so,nvwrite.so,nvcorrect.so,nvtool}` | **build outputs**, and unstripped ones embed DWARF paths from the build machine. Regenerate with `build.sh` / `build-write.sh` |
| `linux-port/beachhead/hr54d`, `linux-port/kexec/kexec-probe` | **build outputs.** Regenerate with `build-hr54d.py` / `build-kexec-probe.py` |
| `userland-mods/755/candidate-patches.md` | a plan for a patch that was never executed. The rejection is recorded in [DEAD-ENDS.md](DEAD-ENDS.md) |
| `**/__pycache__/*.pyc` | build artifacts |

> Note on `wifi/nvwrite.so`: the shipped binary **predated** the current
> `nvwrite.c` — it did not contain the credential assertion the source had.
> Anyone rebuilding gets a different artifact. This is exactly why build
> outputs are not published; the source is the source of truth.

---

## F. Logs and captured state

Large, low-value, and frequently full of receiver-specific detail (PIDs,
serials, addresses). Replaced by short quoted excerpts inside the docs.

| Path | Size | Action |
| --- | --- | --- |
| `userland-mods/755/logs/` (11 files) | 1.4 MB | excluded. `siege-*.maps`, `dvr_core-*.maps`, `/proc/*/fd` dumps, `ps`, `aplDump.txt`, a 1 MB library dump, and a `.tgz` capture |
| `userland-mods/playback/live-state/` | 514 KB | excluded. `aplDump-full.txt` 423 KB, `dt-script.txt` 72 KB, DirectTest logs |
| `userland-mods/navigation/live-state/` (52 files) | 240 KB | excluded |
| `jellyfin/tests/logs/` | — | excluded |
| `logs/sdb2-xfs_ncheck-after-repair.txt` | 15 MB | excluded |
| `poc-hardware/mips-vm-*-console.log` (9 files) | 10–16 KB each | excluded; the findings they establish are in [ROOT.md](ROOT.md) and `poc-hardware` evidence is quoted inline |
| `poc-hardware/{disk-identity,preinstall-*,expected-boot-behavior,offline-verification-*}.txt` | — | excluded; the substantive facts (partition geometry, hashes) are in [HARDWARE.md](HARDWARE.md) and the tables here |
| `jellyfin/remote/service/supervise/{lock,pid,stat,status}` | 0–20 B | excluded. **Runtime PID/lock state of the superseded host service** |
| `jellyfin/tests/`, `emulation/`, `tmp/`, `raw/`, `samples/`, `mounts/`, `kernel/` | — | excluded — blobs, device mounts, and scratch |

---

## G. Documentation that was rewritten rather than copied

Several original docs mixed current state with months of dead ends, and some
contained a real LAN address, a card ID, or a stale hash. These were **rewritten
for a public audience**; the originals are not published verbatim:

| Original | Now |
| --- | --- |
| `jellyfin/HANDOFF.md`, `STATE.md` | merged into [JELLYFIN.md](JELLYFIN.md), [PERSISTENCE.md](PERSISTENCE.md) |
| `jellyfin/ARCHITECTURE.md` | [ARCHITECTURE.md](ARCHITECTURE.md) (the original still described the superseded host proxy as current) |
| `jellyfin/AUTH.md` | [JELLYFIN.md](JELLYFIN.md#authentication-quick-connect) (the original's last line — "persistence across reboot is still unproven" — had been falsified) |
| `jellyfin/ui/STATE.md` | [ITV-WEBKIT.md](ITV-WEBKIT.md). Heavily stale: it claimed no Quick Connect had completed and no TV browser existed |
| `jellyfin/ui/STOCK_SETTINGS.md` | [ITV-WEBKIT.md](ITV-WEBKIT.md) + the Settings tools themselves |
| `jellyfin/remote/server.py` | published as the **superseded host backend**, clearly labelled |
| `poc-hardware/README.md`, `STATE.md` | [ROOT.md](ROOT.md), [ROLLBACK.md](ROLLBACK.md). The originals pinned the v3 image and a stale address |
| `REPORT.md` | [ORIGINAL-REPORT.md](ORIGINAL-REPORT.md), with secrets and personal identifiers redacted |
| `userland-mods/*/README.md` | [DEAD-ENDS.md](DEAD-ENDS.md) and per-topic docs |

---

## If something is still missing

Check `docs/PROPRIETARY_INPUTS.md` — every vendor input is listed there with its
path on your own receiver and the script that consumes it. If you hit an
unexplained dependency, that is a bug in this documentation; please open an
issue rather than guessing.
