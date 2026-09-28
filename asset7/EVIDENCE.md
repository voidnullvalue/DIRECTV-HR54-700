# HR54 hardware PoC state

Last updated: 2026-09-27 CDT, after confirming the v3 beachhead, live inventory, and stock-kernel kexec status on hardware.

## Physical disk state

- The HR54 disk currently enumerates as `/dev/sdb`, but that name is not an identity and must never be assumed. Guarded helpers resolve exactly one disk by 1,000,204,886,016-byte size and USB-bridge serial `<DISK_SERIAL>`, then validate all four partition starts and sizes.
- Only partition 2 was changed. Partitions 1, 3, 4 and the partition table were not exposed to the write guest.
- The disk was unmounted and kernel read-only at the last host-side check. It is now installed in the powered HR54 and mounted live as `/dev/sda2` at `/var`; do not attach it to the host or assume a host device name while it is running.
- The v3 write guest replayed the normal existing XFS journal, verified the v2 image, signature, anchor, and hardware-created proof before writing, and cleanly unmounted. A separate guest then mounted partition 2 `ro,norecovery` and verified v3 plus the proof.
- The authoritative rollback image remains `raw/sdb2.img` and was not modified or rehashed.

Current physical partition-2 hashes:

| File | SHA-256 | State |
|---|---|---|
| `/var/network/plugins/7_6933_6840.squashfs` | `1d8738b02b575e7f7a117b8fed17c1b16f8e8a08362e42531e42a2fcf9722970` | v3 LAN beachhead, verified before boot and mounted live on loop6 |
| `/var/network/plugins/7_6933_6840.sig` | `4dd7b9feb5801c86f5509c6d0b4f8af786613195e7c180e1732b6a2ed1436095` | signature-substitution metadata, installed and read-only verified |
| `/var/network/plugins/7_6932_6932.squashfs` | `fd0c08e0d6c79a1eb92713639dd720b5b01c743933c1b445351ba4ab0647b75e` | genuine verification anchor, preserved and read-only verified |
| `/var/HR54_ROOT_PROOF` | `81a07c0a64ed0af4d0496a089590f3a591df0812220aa8bfbffde115c9e8c90d` | exact 15 bytes `HR54_ROOT_EXEC\n`, created by the real HR54 v2 boot |

Historical malicious image hashes:

- v1 library-constructor image: `6494e28d4b6b359ed40a679d6b8c37e1d60e57febfca7f027529e430440c50ff`
- v2 deterministic indexer proof image: `8fc8c230ab7147d7a85fca7cecd27c86a1b06e9660b2c7dfa2272042d8413bd5`

## Most recent hardware result (v3)

The receiver completed normal boot to the GUI/dish-error state with v2 installed. After the HDD returned to the host, a guarded `ro,norecovery` big-endian XFS view found `/var/HR54_ROOT_PROOF` without requiring journal replay. It was root-owned, mode 0644, 15 bytes, contained exactly `HR54_ROOT_EXEC\n`, and had SHA-256 `81a07c0a64ed0af4d0496a089590f3a591df0812220aa8bfbffde115c9e8c90d`.

This conclusively proves deterministic native code execution as root during ordinary HR54 boot through the plugin signature/path-confusion primitive. The v2 plugin pair and genuine anchor remained hash-correct after that boot.

V3 then completed a normal live boot at `the receiver` (ethernet MAC `<MAC>`). TCP/5777 accepted a shell connection from the LAN host and returned `uid=0(root) gid=0(root)`. `/var/hr54-beachhead/status` reports `LAN_SERVICES_STARTED tcp=5777 udp=1069`, and `/var/hr54-inventory/STATUS` reports `HR54_INVENTORY_COMPLETE`. The inventory was retrieved over the enabled TFTP service and stored locally as `logs/live-hr54-inventory-v3.tar` (SHA-256 `33eae948c1b1d5cc196c40e6065ceefe1d351e597f2ce4970b27bb81d9749f26`).

## Exploit-stage evidence

| Stage | Evidence | Result | Confidence |
|---|---|---|---|
| PoC discovered and wins comparison | Asset 7 manifest minimum is 6839, stack is 6840, and chosen `7_6933_6840` is compatible; genuine `7_6932_6932` has future minimum 6932 | proven | high |
| Signature accepted | `start_mount` creates `7/current` only after `check_deployed_plugin`; the real v1 boot left `7/current` | proven on hardware | high |
| Malicious SquashFS mounted | mount/config failure removes `7/` and the chosen pair; both survived the real boot | proven on hardware | high |
| Asset-7 boot entry point | recovered `Mp4libFeatureStarter` bytecode executes `/opt/mp4lib/bin/indexer` during its init-time availability probe | proven statically | high |
| Indexer runs during ordinary boot | v2 attached its proof to the indexer's called DT_INIT path; real boot created the exact persistent proof | proven on hardware | high |
| Native root execution | root-owned `/var/HR54_ROOT_PROOF` contains exact payload bytes after a normal GUI boot | proven on hardware | conclusive |
| v3 launcher | recovered BusyBox ash writes exact proof/marker; production `sigtst` accepts metadata; genuine library and original indexer are byte-preserved; original exit 22/banner preserved | proven offline | high |
| v3 root listener | 812-byte static MIPS32 big-endian o32 ELF reached socket, bind TCP/5777, listen, accept, fork, dup2, and `/bin/bash -i` execve under QEMU syscall tracing | proven offline through execve | high |
| v3 file transfer | recovered `in.tftpd` parsed and bound `127.0.0.1:1069` under QEMU, then daemonized | proven offline through bind/fork | high |
| v3 root shell | live TCP/5777 session returned root UID/GID on `the receiver` | proven on hardware | conclusive |
| v3 file transfer | live stock TFTP service retrieved the complete inventory archive over UDP/1069 | proven on hardware | high |
| v3 inventory | `/var/hr54-inventory/STATUS` is complete; archive recovered and extracted locally | proven on hardware | high |
| live SoC/RAM | `/proc/cpuinfo` and dmesg: BCM7346B2 STB platform, two Broadcom BMIPS5000 cores at 1305 MHz, 1024 MiB physical RAM; vendor `bmem` reservations leave 544 MiB available to Linux | proven on hardware | high |
| stock kexec | live invalid `kexec_load(0,0,NULL,0)` raw MIPS o32 probe exited 89 (`ENOSYS`); the only two kexec kallsyms are weak syscall stubs | unavailable: `CONFIG_KEXEC` disabled | conclusive |

## Current payload

The v3 asset keeps the genuine `libmp4lib.so` and unmodified vendor indexer as `indexer.real`. Its wrapper first rewrites the root proof, then once per boot:

- starts persistent inventory collection under `/var/hr54-inventory`;
- installs IPv4 firewall accepts for loopback, RFC1918, and link-local sources above explicit drops for TCP/5777 and UDP/1069;
- launches a libc-free root shell listener on TCP/5777;
- launches the stock TFTP daemon rooted at `/var/hr54-transfer` on UDP/1069;
- delegates to the unmodified vendor indexer with the original arguments and behavior.

If firewall setup fails, neither LAN service is launched and `/var/hr54-beachhead/status` records the failure. The listener has no application-layer authentication and is intentionally restricted to a trusted private LAN by firewall rules.

Offline verification is `offline-verification-v3-at-install.txt`. Physical write and independent read-only verification consoles are `mips-vm-v3-install-console.log` and `mips-vm-v3-check-console.log`. Live shell/kexec evidence is `logs/live-kexec-probe.txt` and `logs/live-kexec-probe-execution.txt`.

## Next action

Keep the receiver on the trusted LAN while root-shell collection remains useful. The live inventory has established BCM7346B2, 544 MiB RAM, flash geometry, SATA/XFS layout, and Ethernet addresses. Stock-kernel kexec is conclusively unavailable because its syscall is an `ENOSYS` stub, so the next research transition is a RAM-only bootstrap alternative rather than CFE/flash modification: first inspect the bootloader/kernel for a safe executable-memory or initrd-replacement path and only then design a minimal custom-kernel handoff. Do not modify CFE, MTD, or partition 4.

## Rollback

`raw/sdb2.img` is the untouched authoritative rollback copy for the complete partition. The guarded restore path is `poc-hardware/restore-original-sdb2.sh`; confirm device identity dynamically before using it because that legacy script still names `/dev/sdb2` literally.
