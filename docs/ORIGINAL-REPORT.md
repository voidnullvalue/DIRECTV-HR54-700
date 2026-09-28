> **Redactions applied for publication.** This is the author's original
> security analysis, reproduced with the following removed:
>
> - the receiver `root` password hash recovered from `/etc/shadow`
> - the filesystem path of a live (unencrypted) RSA private key on the device
> - the receiver's LAN address, Ethernet MAC, disk serial, and XFS UUID
> - the DISK power-on hour count
>
> Public-key **fingerprints** (SPKI and certificate digests) are retained: they
> are not secrets and they are useful for identification. The key itself is not
> published. See [NOT_INCLUDED.md](NOT_INCLUDED.md) §C.
>
> The analysis is unchanged. Names of sections and all technical findings are
> the author's.

---

# DIRECTV HR54-700 disk / firmware reverse engineering report

Investigation date: 2026-09-27 CDT. Source: physically owned 1 TB DVR disk, always resolved by size, geometry, and serial rather than a transient host device name. Initial acquisition was read-only. Later hardware-PoC work made controlled normal-XFS writes only to partition 2 through a big-endian MIPS guest; partitions 1, 3, 4 and the partition table were not host-written. The disk was unmounted and host-read-only after the v3 install; it is now installed in the powered receiver as live `/var`.

## 1. Executive result

This HR54-family software is **32-bit big-endian MIPS32/o32**, not ARM. The recovered release is `v1b6840_0x1AB8_370f669`, built into a little-endian SquashFS image dated 2024-06-21 but containing big-endian MIPS executables. Userspace is BusyBox 1.16.1, uClibc 0.9.32.1, and Linux 3.3.8-3.0.

The trusted live OS is primarily in motherboard flash: embedded bootargs say `mtdparts=dtvflash.0:1024k@0k(loader),2048k@1024k(kernel),61440k@3072k(rootfs) root=/dev/mtdblock2 rootfstype=squashfs ... ro`. Nevertheless, HDD partition 4 contains a complete CFE + compressed kernel + SquashFS firmware image, evidently the SWDL staging/recovery payload. HDD partitions 2 and 3 together form `/var`: `sdb2` is the XFS metadata/data device and `sdb3` is its 983.4 GB external XFS real-time device used for recordings.

An HDD-only path to deterministic native root execution and a usable live development beachhead are now proven on the physical receiver. The plugin installer verifies the pathname contained in unsigned `.sig` metadata, then mounts a different, filename-derived `.squashfs`. A real receiver boot left the attacker pair intact and recreated `/var/network/plugins/7/current`; the failure branch would remove both, proving signature acceptance and malicious mounting. The first payload produced no proof because it incorrectly assumed `Mp4libFeatureStarter` loaded `libmp4lib.so`. Recovered Java bytecode instead showed a boot initialization probe that directly executes `/opt/mp4lib/bin/indexer`. A corrected indexer-based v2 payload then created the exact persistent root-owned file `/var/HR54_ROOT_PROOF` containing `HR54_ROOT_EXEC\n` during a normal GUI boot. V3 then exposed a live root shell at TCP/5777 and stock TFTP at UDP/1069, retrieved a complete inventory, and established that stock-kernel kexec is disabled. Dirty COW remains a confirmed second stage for any separate non-root foothold but is no longer required for this disk path.

## 2. Exact disk layout

MBR/DOS disk identifier `0x0000103e`, 512-byte logical / 4096-byte physical sectors:

| Partition | Inclusive sectors | Bytes | MBR type | Actual role |
|---|---:|---:|---|---|
| sdb1 | 64-1,060,289 | 542,835,712 | 0x82 | Big-endian Linux swap v1 |
| sdb2 | 1,060,296-32,531,624 | 16,113,320,448 | 0x83 | XFS `/var` data/metadata device, UUID `<XFS_UUID_REDACTED>` |
| sdb3 | 32,531,632-1,953,246,959 | 983,406,247,936 | 0x83 | External XFS real-time device; recording extents |
| sdb4 | 1,953,246,960-1,953,520,064 | 139,829,760 | 0xab | SWDL firmware staging: CFE, kernel, SquashFS rootfs |

There are 32 KiB before sdb1, 3 KiB and 3.5 KiB inter-partition gaps, and 2,552 sectors after sdb4. No GPT or LVM exists. Evidence: `logs/fdisk.txt`, `logs/parted-sectors.txt`, `logs/blkid.txt`, `logs/wipefs.txt`, `logs/file-source.txt`.

## 3. Imaging and integrity

GNU ddrescue acquired sdb1, sdb2, and sdb4 at 100% with zero read errors/bad areas. The first and last 512 MiB of sdb3 were also acquired cleanly. Maps are beside images in `raw/` and `samples/`.

Pristine SHA-256 values recorded before XFS log clearing:

- `sdb1.img`: `87c8dbe133379b7ed980258b9569187ccefd6d2c8aab1638364fd8a4a9147640`
- `sdb2.img`: `1b93be57da0aa5a07eb54bbf900a3bb6d778e0c1a82aee4472223e304737c0ad`
- Remaining small-image digests: `logs/SHA256SUMS.small-images`

After hashing, only the copied `raw/sdb2.img` had its dirty XFS log cleared with `xfs_repair -L`; the command used a correctly sized sparse real-time placeholder. The source disk was unaffected. The repaired copy was mounted through read-only `/dev/loop0` with `ro,norecovery,nouuid`, and the placeholder through read-only `/dev/loop1` as `rtdev`.

SMART passed with zero reallocated, pending, or offline-uncorrectable sectors. The drive has a very high power-on hour count (redacted). Full output: `logs/smart-default.txt`.

## 4. Partition purposes and recovered content

- **sdb1 — LOW VALUE:** swap only; binwalk hits are coincidental remnants.
- **sdb2 — VERY PROMISING:** persistent `/var`, including `swdl/images`, `network/plugins`, databases, registries, logs, SSH state, APG guide data, recording metadata, and writable service configuration. XFS superblock: 4 KiB blocks, 256-byte inodes, 4 allocation groups, root inode 192, 1,516,773 allocated data blocks.
- **sdb3 — LOW VALUE for root / valuable for recording recovery:** XFS real-time extent device. The first sample contains recording-related printable fragments; the final sample is effectively high entropy/no strings. A complete 916 GB copy is not presently justified for code-execution research.
- **sdb4 — VERY PROMISING:** firmware staging/recovery image. Binwalk finds CFE strings near `0x4f6d`, LZMA data at `0xe6f9`, and a valid SquashFS at decimal `2,107,433` (`0x202829`), image size 33,703,728 bytes. Extracted to `extracted/sdb4-rootfs/`.

The sdb2 XFS pathname index contains 185,665 records (`logs/sdb2-xfs_ncheck-after-repair.txt`). Two cached SWDL packages exist:

- `/swdl/images/image_mfr-78_mdl-2b_ver-1abc_tar-ffff.csw`: 12,374,144 bytes; Pace `C41`, build `PI25_R7_V3`, dated 2024-05-01.
- `/swdl/images/image_mfr-78_mdl-3b_ver-1abc_tar-ffff.csw`: 17,715,328 bytes; Pace `C61K`, build `PI34_R62_V2-1-g7efd894`, dated 2024-05-03.

These are client firmware packages, not the HR54 firmware, but are valuable format samples.

## 5. Architecture / software inventory

All inspected executables and libraries are ELF32, big-endian MIPS32, o32 ABI, dynamic interpreter `/lib/ld-uClibc.so.0`. There are 319 ELF objects in `inventory-elf.tsv`. Key versions/artifacts:

- Linux modules: `3.3.8-3.0`
- BusyBox: `1.16.1`, build timestamp 2023-03-15
- uClibc: `0.9.32.1`
- PPPD: `2.4.4`
- zlib: `1.2.3`; bzip2: `1.0.6`; SQLite library `libsqlite3.so.0.8.6`
- WolfSSL library SONAME/file `libwolfssl.so.19.0.0` (exact upstream release not established)
- libcurl ABI `libcurl.so.4.4.0` (upstream release string not established)
- Dropbear client `dbclient`; no Dropbear server binary in this production SquashFS
- Proprietary components: `dvr_core`, `dtv`, `siege` VM, `swdl`, `verifier`, `ggup`, `dhttp`, `upnp_proxy`, `pms`, `dms`, `moca_manager`, `wvbmanager`, Widevine, DTCP, CA manager, and BIST.

Full type list: `logs/rootfs-file-types.txt`; files by size: `logs/rootfs-files-by-size.tsv`; unsafe imports: `logs/unsafe-imports.tsv` (173 objects).

## 6. Boot sequence and storage trust boundary

1. Broadcom CFE boots a kernel/rootfs from internal `dtvflash` MTD.
2. Kernel mounts `/dev/mtdblock2` as read-only SquashFS.
3. `/sbin/init` is BusyBox; `/etc/inittab` executes `/etc/init.d/rcS`.
4. `rcS` loads Broadcom/CDI modules, constructs `/tmp/fstab`, checks/repairs the disk, mounts sdb2 as `/var` with `rtdev=sdb3`, imports persistent data, verifies/deploys plugins, then starts `/etc/init.d/S??*` services.
5. `sdb4` is the type-0xab SWDL partition. `/root/partition_info.sh` explicitly names partition 4 `swdl` and allocates ~150 MB.

Answers to the requested boot questions:

1. `/sbin/init` on HDD? **A copy is in sdb4 firmware, but live init is loaded from internal MTD rootfs.**
2. Startup scripts on HDD? **Firmware copies are in sdb4; mutable `/var` does not replace `/etc/init.d`.**
3. HDD-only startup alteration? **Not by a plain rc script found so far. Signed plugins and writable configuration are indirect avenues.**
4. Signatures checked? **Yes for BIST USB payloads and plugins; full SWDL verification needs deeper reversing.**
5. Kernel on HDD? **Yes, compressed kernel in sdb4, but bootargs indicate live kernel/rootfs normally reside in MTD.**
6. Bootloader on HDD? **A CFE image/string region is in sdb4; live first-stage CFE is expected in flash.**
7. Bootloader environment on HDD? **No standalone writable CFE environment was identified.**

## 7. Authentication, credentials, and debug paths

`/etc/shadow` contains root MD5-crypt `$1$<REDACTED>`. The bundled John dictionary did not crack it. All other visible accounts are locked (`*`). Root shell is `/bin/ash`; `/etc/securetty` exists, but production inittab does not spawn a getty.

A complete unencrypted 2048-bit RSA private key exists at `/opt/ggup/bin/<GGUP_TEST_TLS_KEY_REDACTED>`. It exactly matches the self-signed CA certificate beside it, `gcupu_server_cert_test`. This is conclusively a **GGUP TLS test-server identity**, not a boot, CSW, or plugin signing key. `libggup_common.so::WolfGgupSsl::wolfSslServerCtxNew` selects the pair only for `TlsMode_test`; production TLS mode obtains `gcupu_server_cert` and its trust path through SSM. No byte-exact modulus or fingerprint match occurs elsewhere in the rootfs or cached update/plugin corpus.

RSA public-key fingerprints (SPKI DER): SHA-1 `ca08ea622c4d6007e778cc68ffed7fa873de2caf`; SHA-256 `55f151b29249a0f98afe64edcfc7b0e8f482f1b91c3926c96ee2632a5adf302d`. Certificate SHA-256 is `de7c988acb52a7197a41694f9ef9a443358e8c72abd3dfe2ddcaccec4509d550`. The certificate is CA:TRUE, self-issued `ATT/DirecTV/gcupu`, and expired 2025-12-03. Evidence: `logs/test-key-*`, `logs/test-cert-details.txt`, and `logs/test-key-byte-matches-{rootfs,corpus}.tsv` (the latter two are empty).

Debug/development paths:

- `rcS::mount_development_cdidrivers()` accepts `/mnt/insmod.sh` and arbitrary CDI content from VFAT USB, but only when `IMAGE_MODE != production`. In this exact rootfs `/root/getmode.sh -i` is compiled/generated as a literal `production`, so mutable HDD configuration cannot switch it; reaching this path requires a development rootfs, bootarg/rootfs override, or verifier bypass (**BLOCKED on this production image**).
- Development bootargs support NFS override of `/lib/modules/cdi` through `cdipath=` and `cdinfsopt=`.
- BIST scans USB for `/mnt/update_bist.sh` plus a platform signature and for `*_${manufacturer}_${model}_*.flash.ec`; signatures are checked.
- Writable marker/test paths such as `/var/viewer/si_723.sh`, `/var/autostress_boot/si_706run.sh`, and `/var/viewer/si_706run.sh` alter diagnostics. The inspected `psmem.sh` only tests existence of `si_723.sh`, not executes it; other call paths require review.

## 8. Update and plugin verification

`/root/install_plugin.sh` manages `/var/network/plugins`. It requires paired `.squashfs` and `.sig`, calls `/opt/sig/bin/sigtst`, then loop-mounts the SquashFS under `<asset-id>/current`. `sigtst` implements SHA-1 plus 1024/160-bit DSS, expects 80 hex characters, and embeds three distinct public values sharing common P/Q/G parameters: key type 0 HMC/private-apps/server, type 1 client, and type 2 common-apps. Current plugin `13_6932_6932.squashfs` validates with key type 0; the installer accepts type 0 first and falls back to type 2. `tools/plugin_verify.py` reproduces verification offline.

The signature boundary is broken. A `.sig` file contains plaintext `IMAGE`, `SIGNATURE`, and `SIZE`. These metadata fields are not authenticated. `sigtst` opens and hashes the path named by `IMAGE`; the caller does not require it to equal the same-basename SquashFS it later mounts. `check_deployed_plugin()` verifies `<chosen>.sig`, but `start_mount()` then mounts `<chosen>.squashfs`. This is a confirmed confused-deputy/signature-substitution flaw.

The exact boot selection was re-traced rather than inferred from an earlier summary. `boot_time_deploy(1)` removes stale asset mount directories and enumerates each `*.squashfs`. Asset 7's manifest minimum is 6839 and the running stack is 6840. Thus `7_6933_6840` is compatible and selected, whereas genuine `7_6932_6932` is retained as a future image because its minimum stack 6932 exceeds 6840. HDD-present mode permits asset 7 and the observed GUI boot was not failsafe. Post-boot `/var/network/plugins/7/current` is decisive persistent evidence: it is created only after successful verification, while a mount/config failure undeploys the directory and deletes the selected pair.

The first native trigger assumption was wrong. `dtv.car` bytecode for `Mp4libFeatureStarter.start()` invokes `checkIfDaiIndexerAvailable()`. That method executes `/opt/mp4lib/bin/indexer`, drains stdout and stderr, waits, and searches for `Simple IDR frames indexer`; it never references or loads `libmp4lib.so`. `CommonPluginHandler.initAllFeatures()` iterates the mounted config's features and issues `FeatureStartCommand` for entries whose `needInit()` is true; asset 7 declares mp4lib `init="true"`. `libdvr.so` references `libmp4lib.so` together with `RemoteMediaItem`/`RemoteMp4MediaStream` and MP4-service strings, consistent with a later media/playback path rather than the plugin starter.

The v1 modified library is valid MIPS32 big-endian o32 and its DT_INIT patch executes under QEMU, reaching the intended fork and `execve`. Its shell script is LF-clean and `/bin/sh` exists, but it originally delayed the persistent write until after volatile `/var/tmp` diagnostics. The corrected v2 does not depend on that path: it restores the genuine `libmp4lib.so` and patches the indexer's called DT_INIT helper to perform raw o32 `open/write/close/sync` syscalls for `/var/HR54_ROOT_PROOF`, then return to normal indexer behavior. Under the recovered rootfs it creates exactly `HR54_ROOT_EXEC\n`, preserves the expected banner and original exit status 22. Production `sigtst` returns 0. V2 SquashFS SHA-256 is `8fc8c230ab7147d7a85fca7cecd27c86a1b06e9660b2c7dfa2272042d8413bd5`; the unchanged `.sig` is `4dd7b9feb5801c86f5509c6d0b4f8af786613195e7c180e1732b6a2ed1436095`.

V2 then succeeded on the real HR54. Following a normal boot to the GUI/dish-error state, a guarded read-only big-endian XFS inspection found a root-owned, mode-0644, 15-byte `/var/HR54_ROOT_PROOF` without needing journal replay. Its exact content is `HR54_ROOT_EXEC\n` and SHA-256 is `81a07c0a64ed0af4d0496a089590f3a591df0812220aa8bfbffde115c9e8c90d`. This is conclusive hardware proof of root execution during ordinary boot.

V3 preserves the genuine `libmp4lib.so` and original indexer, wrapping the latter with a boot launcher that collects persistent inventory, installs private-LAN-only firewall rules, starts a libc-free MIPS root shell on TCP/5777, and starts the stock TFTP daemon on UDP/1069. Its listener reached `socket`, `bind`, `listen`, `accept`, `fork`, `dup2`, and `/bin/bash -i` `execve` under QEMU syscall tracing. Production `sigtst`, recovered BusyBox ash, unmodified-indexer behavior, ELF/ABI checks, and TFTP bind/daemonization all pass offline. V3 SquashFS SHA-256 is `1d8738b02b575e7f7a117b8fed17c1b16f8e8a08362e42531e42a2fcf9722970`; it was installed on physical partition 2 and independently verified `ro,norecovery`.

V3 succeeded on hardware. The receiver came up as `the receiver` with ethernet MAC `<MAC_REDACTED>`; the TCP/5777 session returned `uid=0(root) gid=0(root)`, and its status records `LAN_SERVICES_STARTED tcp=5777 udp=1069`. The live inventory completed and was retrieved over TFTP to `logs/live-hr54-inventory-v3.tar` (SHA-256 `33eae948c1b1d5cc196c40e6065ceefe1d351e597f2ce4970b27bb81d9749f26`). It identifies BCM7346B2, two BMIPS5000 cores at 1305 MHz, 1024 MiB physical RAM with 544 MiB available after 192 MiB, 224 MiB, and 60 MiB vendor `bmem` reservations, MTD loader/kernel/rootfs/oops/SSD/all geometry, SATA-backed XFS `/var`, and the active plugin loop mounts. `/proc/kallsyms` contains only weak `sys_kexec_load`/`compat_sys_kexec_load` stubs. A raw MIPS o32 invocation of the deliberately invalid `kexec_load(0,0,NULL,0)` exited 89 (`ENOSYS` on MIPS), rather than `EINVAL`; stock `CONFIG_KEXEC` is therefore disabled and this kernel cannot provide the preferred kexec handoff.

CSW findings from the two real cached client packages:

- v4/C41 is 12,374,144 bytes; metadata length `0x19c`, alignment `0x1000`, two payloads; top-level LZMA kernel at `0x40000`, SquashFS/LZMA root at `0x1a0000`, fixed 128-byte signature trailer at `0xbcd000`.
- v5/C61K is 17,715,328 bytes; payload 1 offset/length `0x80000/0x1894b8`; payload 2 `0x240000/0xea5000`; XZ kernel/DTB area and XZ SquashFS root; signature trailer at `0x10e5000`.
- Both trailers contain a DER pair of approximately 240-bit integers, not the exposed RSA key and not the 160-bit raw DSS plugin format. The v5 second declared payload ends exactly at the signature trailer and includes filesystem padding. `tools/csw_inspect.py` emits the structural map in `logs/csw-structure.json`.
- These C41/C61K CSWs are served by the HR54, not installed as its own rootfs. No HR54 binary references `.csw` or parses these headers; production TFTP serves them to client receivers whose parser/verifier is inside those clients. Therefore their exact production verifier is **not present in the HR54 corpus** and acceptance of any signing root cannot be established from this disk.

Remaining review candidates:

- `/opt/plugins/bin/plugin` parses attacker-controlled `.plugin`/tar input before final deployment.
- `install_plugin.sh` uses `tar -tf` and filename parsing; its optimized base/spec patch path and tar member handling need traversal/symlink testing on a copied `/var` (**PROMISING parser avenue**).
- `.plugin` container and simple-plugin tar paths still deserve traversal/symlink fuzzing, but they are no longer needed for the offline-HDD exploit.
- `/opt/bist/bin/bist_main` implements `flashUpdate`; reverse its `.flash.ec` decrypt/verify/write order.
- Cached `.csw` packages give real parser corpus; locate the HR54 `.csw` over satellite/network or from `/var/swdl` during an update.

## 9. Network attack surface

Confirmed init-started services include portmap, standalone HPA TFTP, Avahi auto-IP, network/MoCA managers, proprietary middleware, DLNA/UPnP, DHTTP/SHEF, DMS/PMS, WVB management, and DVR/RTSP components. `/etc/init.d/S80tftpd-hpa` starts `tftp-hpa 0.40` on every boot as default user `nobody`; on HMC it uses `-c -l -s /var/swdl/images`. The firewall explicitly admits UDP/69 and the cached CSWs are mode 0666, so an unauthenticated LAN peer can overwrite existing client firmware cache objects. This is a confirmed arbitrary-write primitive inside the TFTP root, but not an HR54 code-execution primitive: the files are served to C41/C61K clients and the HR54 does not parse them.

Highest-priority proprietary targets based on network role plus dangerous imports:

- **VERY PROMISING:** `libjhupnp.so` (UPnP plus `system`, `strcpy`, `sprintf`)
- **PROMISING:** `libpms.so`, `librtspmodule.so`, `dlnawrapperservice`, `dhttp` libraries, `ca_manager_srv`
- **PROMISING:** `mocad` / `netdiagd` and `tcmanager` (`system`/`popen` plus formatted input)
- **POSSIBLE:** `libdvr.so`, `libsiege.so`, middleware IPC/JNI libraries
- **LOW VALUE alone:** import presence without a reachable attacker-controlled call path

No exploit is claimed merely from imported symbols. Runtime port capture (`netstat -lntup` on the powered receiver) is needed to map listeners to PIDs and choose concrete call paths.

## 10. Static vulnerability candidates and CVEs

Confirmed version-range matches, subject to vendor backport uncertainty:

- **CVE-2016-5195 / Dirty COW — CONFIRMED PRESENT, not merely version-range:** the exact decompressed ELF kernel is `kernel/kernel.bin`, BuildID `62c073e4b2f4005d7f6e3c1c4a4d9ecc734c4aa9`. Export-table recovery locates `__get_user_pages` at `0x800a5a10` and `get_user_pages` at `0x800a5e18`. Disassembly shows fault flags passed by value at `0x800a5bcc` and no post-COW clearing of `FOLL_WRITE`; `s0` remains unchanged except an unrelated `FOLL_TRIED` OR. The upstream Dirty COW correction requires mutating the flags after a forced COW. This vendor build therefore lacks that fix despite its 2023 build date. Evidence: `logs/kernel-__get_user_pages-disassembly.txt`, `logs/kernel-exported-symbols.tsv`.
- **CVE-2020-8597 — CONFIRMED VERSION RANGE, exposure-dependent:** PPPD 2.4.4 is within vulnerable 2.4.2-2.4.8; EAP packet processing can overflow a buffer. Whether an attacker can reach PPP EAP here is unconfirmed. NVD: https://nvd.nist.gov/vuln/detail/CVE-2020-8597
- **CVE-2018-20679 — CONFIRMED VERSION RANGE:** BusyBox 1.16.1 precedes 1.30.0; crafted DHCP options can cause an unauthenticated stack information leak in udhcp. The receiver uses DHCP client paths. NVD: https://nvd.nist.gov/vuln/detail/CVE-2018-20679
- **CVE-2022-30295 — CONFIRMED VERSION RANGE:** uClibc 0.9.32.1 is at or below 0.9.33.2; predictable DNS transaction IDs permit DNS cache poisoning under suitable network conditions. NVD: https://nvd.nist.gov/vuln/detail/CVE-2022-30295

The kernel and userspace are old enough to have many more CVEs, but entries are not marked exploitable without config/reachability evidence.

## 11. Ranked paths to code execution/root

1. **PROVEN ON HARDWARE — plugin signature substitution to native root and live beachhead:** the real HR54 accepted the substituted signature, mounted the malicious higher-version asset, ran the corrected v2 indexer hook as root during ordinary startup, and created the exact persistent proof. V3 then provided a confirmed private-LAN root shell, TFTP file transfer, and persistent inventory collector.
2. **VERY PROMISING — network daemon bug then confirmed Dirty COW:** runtime-map unauthenticated UPnP/DHTTP/SHEF/RTSP/MoCA services, reverse the corresponding unsafe call paths, gain any user execution, then use the confirmed-unpatched `get_user_pages` implementation for root.
3. **PROMISING — plugin download/archive path:** deliver signature-substitution files through the APG/CDN `.plugin` or simple-plugin tar workflow, or exploit its pre-verification extraction, converting the offline path into remote persistence.
4. **POSSIBLE — writable `/var` configuration injection:** inspect middleware registries/properties for shell commands, library paths, diagnostic program names, and service endpoint overrides. `getmode.sh` reads `powerToolsAccessValue` from writable `/var/druid_data/druid.properties`, which can enable CE/power-access behavior, although reviewed consumers currently change logging/core collection rather than spawn a shell. No direct `source /var/...` primitive has yet been proven.
5. **POSSIBLE — root password/console:** the recovered `/etc/shadow` MD5-crypt hash is **redacted from this publication** (see the note at the top) and is not worth pursuing — it resisted a stock dictionary, and a valid password only helps if a getty/debug SSH path can be enabled. UART capture remains the more direct route.
6. **BLOCKED — production-to-development switch via HDD:** development mode would directly enable USB/NFS-provided `insmod.sh`, but this release hardcodes `getmode.sh -i` to `production`; it is not a writable registry setting.
7. **LOW VALUE — RSA test key / modify sdb4 blindly:** the RSA key is restricted to GGUP test TLS and does not sign plugins or cached CSWs; internal MTD boot means arbitrary sdb4 edits are unlikely to boot.

## 12. Secure/verified boot assessment

Application/plugin and BIST update verification are proven. Full hardware secure-boot enforcement is **not yet proven** from the HDD alone. The CFE/rootfs contains verification strings and signed payload machinery, but fuse/OTP policy and first-stage CFE behavior live on the motherboard. Distinguish:

- plugin SquashFS: authenticated by DSA/SHA-1 (`sigtst`)
- BIST USB payload: signature checked
- `.flash.ec`: encrypted/signed-looking and handled by BIST `flashUpdate`
- kernel/rootfs in live MTD: likely authenticated during SWDL/flash update; boot-time enforcement still needs UART/SPI evidence
- mutable `/var`: not globally hashed; individual plugins/payloads are checked

## 13. Missing information / next experiments

1. Keep the live beachhead available while evaluating a RAM-only handoff alternative. Stock-kernel kexec is conclusively unavailable: its MIPS o32 `kexec_load` syscall returns `ENOSYS` and only weak stubs are present. Do not modify CFE, MTD, or partition 4.
2. Dump the motherboard SPI NOR/NAND/eMMC in-circuit or via bootloader/JTAG. The bootargs predict a 1 MiB loader, 2 MiB kernel, and 60 MiB rootfs MTD layout. Identify the Broadcom SoC/flash chip markings before choosing voltage/pinout.
3. On a powered isolated receiver, capture `netstat -lntup`, `/proc/cmdline`, `/proc/mtd`, mounts, process UIDs, `getmode.sh -i/-w/-fm`, and firewall rules.
4. Extract and disassemble `sigtst`, `plugin`, `bist_main::flashUpdate`, `libswdl.so`, UPnP, DHTTP, and RTSP handlers in Ghidra using MIPS big-endian o32.
5. Fuzz the two recovered `.csw` files and synthetic plugin tarballs against emulated/parser-only harnesses; specifically test validation order, integer lengths, path traversal, and decompression before authentication.
6. Exercise the GGUP diagnostic TLS-mode action on an isolated receiver to determine whether production authorization permits switching to `TlsMode_test`; the key/certificate identity itself is already fully matched.
7. Continue targeted root hash cracking with model/vendor/date-derived candidates; the small stock dictionary failed.

## 14. Reproducibility / evidence index

- Acquisition tool: `tools/acquire_phase1.sh`
- Repeatable scanners: `tools/image_scan.sh`, `tools/elf_inventory.sh`, `tools/interesting_strings.sh`, `tools/crypto_locator.sh`
- New focused tools: `tools/modulus_scan.py`, `tools/csw_inspect.py`, `tools/plugin_verify.py`
- Real update/plugin corpus: `corpus/csw/`, `corpus/plugins/`
- Reproduced plugin exploit artifacts: `poc/7_6933_6840.{squashfs,sig}`, `emulation/plugin-test/`, `logs/plugin-poc-verifier.txt`
- Physical PoC state, hardware root proof, live beachhead, and kexec result: `poc-hardware/STATE.md`, `poc-hardware/corrected-plugin/`, `poc-hardware/beachhead-plugin/`, `poc-hardware/offline-verification-v3-at-install.txt`, `poc-hardware/mips-vm-{evidence,v3-install,v3-check}-console.log`, `logs/live-hr54-inventory-v3.tar`, `logs/live-kexec-probe{,-execution}.txt`, `linux-port/{beachhead,kexec}/`
- Extracted exact kernel and GUP evidence: `kernel/kernel.bin`, `logs/kernel-__get_user_pages-disassembly.txt`, `logs/kernel-exported-symbols.tsv`
- Partition/SMART evidence: `logs/fdisk.txt`, `logs/parted-sectors.txt`, `logs/smart-default.txt`
- sdb4 signatures: `logs/binwalk-sdb4.txt`, `logs/strings-sdb4.txt`
- Extracted firmware: `extracted/sdb4-rootfs/`
- Complete ELF inventory: `inventory-elf.tsv`
- Crypto inventory: `inventory-crypto.txt`
- Unsafe imports: `logs/unsafe-imports.tsv`
- XFS path inventory: `logs/sdb2-xfs_ncheck-after-repair.txt`
- Rootfs tree and types: `logs/rootfs-tree-depth2.txt`, `logs/rootfs-file-types.txt`
- Key scripts supporting conclusions: `extracted/sdb4-rootfs/etc/init.d/rcS`, `root/setup_hdd.sh`, `root/install_plugin.sh`, `root/bist.sh`, `etc/init.d/S80tftpd-hpa`, `etc/init.d/config-fstab.sh`.

## 15. Concrete Exploitation Paths

### PROVEN — offline-HDD plugin signature substitution to native root execution

- **Entry point:** `/etc/init.d/rcS:453` calls `/root/install_plugin.sh deploy_boot` as root during every boot.
- **Attacker-controlled input:** paired files under writable persistent `/var/network/plugins`: a chosen `<asset>_<version>_<minimum>.squashfs` and same-basename `.sig`. This is directly controllable by offline modification of a copied/restored HDD; remote delivery still needs the plugin download path.
- **Affected code/function:** `install_plugin.sh::verify_plugin_sig()` at lines 237-255 invokes `sigtst <chosen>.sig`; `sigtst` opens the unsigned `IMAGE =` pathname. `start_mount()` at lines 1248-1324 independently mounts `$PATH_PLUGINS/$EXTRACTED_SQUASHFS`, derived from the chosen filename rather than the verified pathname.
- **Privilege:** installer/mount is root. The resulting plugin supplies VM `.car` code and, for asset 7, executable MIPS binaries and shared objects (`mp4lib/bin/indexer`, `libmp4lib.so`, HLS/DCDP libraries) to root middleware.
- **Evidence/reproduction:** `emulation/plugin-test/evil.sig` verifies a genuine file while `evil.squashfs` is different (`logs/plugin-confused-deputy-reproduction.txt`). On real hardware, v1 survived boot and left installer-created `7/current` even though failure cleanup would delete both. V2 pointed the same accepted `.sig` at genuine `/var/network/plugins/7_6932_6932.squashfs`; after a normal hardware boot it created root-owned `/var/HR54_ROOT_PROOF` with exact content `HR54_ROOT_EXEC\n` and SHA-256 `81a07c...c90d`. V3 adds the development beachhead and is installed/read-only-verified (`poc-hardware/STATE.md`).
- **Required preconditions:** genuine referenced plugin remains present; chosen filename must pass manifest/version rules. For this firmware, manifest minimum for asset 7 is 6839 and stack version is 6840, so `7_6933_6840` is selected over `7_6932_6932` and is compatible. On-device proof must be done against a cloned disk.
- **Result:** the mp4lib initialization probe runs attacker-controlled native code as root and creates a persistent proof without a signing key or MTD modification.
- **Remaining unknowns:** no root/beachhead gate remains. V3's firewall, TCP listener, TFTP daemon, and inventory collector all ran successfully on the receiver; the remaining platform question is a safe RAM-only handoff now that stock kexec is unavailable.
- **Confidence:** **conclusive** for authentication bypass, malicious root mount, boot-time indexer execution, and native root code execution.

### VERY PROMISING — unauthenticated service foothold followed by confirmed Dirty COW root

- **Entry point:** production firewall-admitted services, especially UPnP/SSDP multicast, proprietary DHTTP/SHEF, RTSP (`10800:10803`, `1930:1933`, UDP `6900:6915`), GGUP (`8385/8386`), MoCA management, and TFTP UDP/69.
- **Attacker-controlled input:** unauthenticated LAN packets. `/etc/init.d/S47iptables` is the exact allowed-port source.
- **Affected binary/function:** initial parser bug is not yet isolated. Priority targets remain `libjhupnp.so`, `librtspmodule.so`, GGUP request readers, `mocamanager/netdiagd`, and DHTTP consumers. The second stage is exact kernel `__get_user_pages` at `0x800a5a10`.
- **Privilege:** service-dependent for stage one; kernel exploitation yields root regardless of daemon UID. TFTP itself drops to `nobody`.
- **Evidence:** kernel ELF and disassembly prove absence of the Dirty COW flags mutation. Firewall evidence identifies reachable ports, not merely imported socket APIs.
- **Required preconditions:** one memory-corruption/command/path primitive in an admitted daemon plus a MIPS Dirty COW implementation adapted to this uClibc userspace.
- **Likely result:** LAN-to-user execution, then root and persistent installation through the plugin bypass.
- **Remaining unknowns:** live listener-to-PID/UID mapping and a concrete first-stage parsing defect.
- **Confidence:** **high** for the local root stage; **possible** for the as-yet-unproven remote first stage.

### PROMISING — remote plugin delivery combined with signature substitution

- **Entry point:** APG/CDN plugin handling invoking `install_plugin.sh deploy`, `deploy_simple_plugin`, or post-boot deployment.
- **Attacker-controlled input:** `.plugin` or `.tgz` container, member names, extracted `.sig` metadata, and SquashFS.
- **Affected code/function:** `sw_download_deploy_simple()` extracts with `tar zxvf` before authentication and then calls `check_deployed_plugin`; `start_deploy()` invokes `/opt/plugins/bin/plugin x` before verification. Both ultimately reach the confused-deputy verifier.
- **Privilege:** root shell script and root mounts.
- **Evidence:** exact control flow in `root/install_plugin.sh`; production announcement file marks the main plugins `CDN_only=true`.
- **Required preconditions:** control/spoofing of the authenticated plugin transport, a transport validation weakness, or a traversal/write bug in pre-auth extraction.
- **Likely result:** remote placement of the two files required by the reproduced HDD exploit, then persistent root code on next deployment/boot.
- **Remaining unknowns:** transport authentication/pinning and whether the plugin container extractor sanitizes all names.
- **Confidence:** **medium** as a chain; the terminal signature bypass is confirmed.

### PROMISING for client compromise, LOW VALUE for HR54 root — writable TFTP CSW cache

- **Entry point:** UDP/69, `tftp-hpa 0.40` started with `-c -l -s /var/swdl/images`.
- **Attacker-controlled input:** TFTP WRQ contents and filenames within the chroot; existing CSWs are world-writable.
- **Affected binary/function:** `usr/sbin/in.tftpd`; files are subsequently downloaded by C41/C61K clients.
- **Privilege:** daemon defaults to `nobody`; directory/files are owned by UID 65534 and permit writes.
- **Evidence:** init script, firewall rule, emulated `in.tftpd -V`, and on-disk modes.
- **Required preconditions:** LAN/MoCA reachability. Client-side code execution additionally requires a CSW verifier/parser bypass on the C41/C61K.
- **Likely result:** deterministic client firmware-cache poisoning/denial of service; possible client compromise if parsing precedes trailer authentication.
- **Remaining unknowns:** client verifier implementation and signing key. It is not contained in this HR54 rootfs.
- **Confidence:** **high** for arbitrary cache overwrite, **low** for HR54 code execution.

### BLOCKED — exposed RSA key as firmware/plugin signing root

- **Entry point/input:** GGUP `TlsMode_test` only.
- **Affected code:** `libggup_common.so::WolfGgupSsl::wolfSslServerCtxNew` loads `/opt/ggup/bin/gcupu_server_cert_test` and `<GGUP_TEST_TLS_KEY_REDACTED>` only in the test-mode branch.
- **Evidence:** exact certificate modulus match, byte-exact negative corpus scan, and independent plugin DSS verification. CSW trailers use a different DER signature form.
- **Result:** the private key can impersonate the test GGUP endpoint if test mode is enabled; it cannot forge production plugins or either observed CSW.
- **Remaining unknown:** whether an authenticated diagnostic action can switch a running production GGUP service to test mode. Even then this is transport identity, not package signing.
- **Confidence:** **high** that this is not the signing-root shortcut.

## 16. Bottom line

The plugin verifier's pathname confusion now yields deterministic native root execution and a usable live development beachhead on the real receiver. The missing v1 proof was traced to a wrong loader assumption, not authentication or mount failure; the feature starter's actual boot action is an indexer availability execution. Corrected v2 created the exact persistent proof during a normal GUI boot. V3 supplied the private-LAN shell, TFTP endpoint, and inventory collector during its live boot. The exact BCM7346B2 platform is now identified, and a safe invalid-call probe proves stock kexec is unavailable (`ENOSYS`). The next transition is research into a RAM-only alternative; CFE, MTD, and partition 4 remain untouched.
