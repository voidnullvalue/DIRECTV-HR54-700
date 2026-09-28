# Hardware

## Platform

| Property | Value |
| --- | --- |
| SoC | Broadcom **BCM7346B2** |
| CPU | dual-core **BMIPS5000**, 1305 MHz |
| Architecture | **MIPS32 big-endian (o32)** — *not* ARM |
| ABI | uClibc, `/lib/ld-uClibc.so.0` |
| Kernel | Linux **3.3.8-3.0** |
| Userspace | BusyBox 1.16.1, uClibc 0.9.32.1 |
| Firmware release | `v1b6840_0x1AB8_370f669` |
| RAM | 1024 MiB total, ~544 MiB available to the OS |
| Graphics | Broadcom CDI / NEXUS hardware video pipeline → HDMI |

> ### Build for **MIPS32**, never MIPS32r2
>
> Cross-compiling for `mips-linux-musleabi` defaults to a newer ISA revision on
> modern toolchains. An `-mcpu=mips32r2` build of the native backend **compiles,
> links, and runs correctly under a host emulator, then dies with
> `Illegal Instruction` on the real receiver.** This cost real debugging time
> and is recorded in [DEAD-ENDS.md](DEAD-ENDS.md).
>
> ```sh
> zig cc -target mips-linux-musleabi -mcpu=mips32 -static -O2 -o hr54-jf hr54_jf.c
> ```
>
> There is no workaround short of pinning the ISA. Pin it.

Notable userspace components: PPPD 2.4.4, zlib 1.2.3, bzip2 1.0.6, SQLite,
WolfSSL, libcurl, Dropbear (client only — **no SSH server**). Proprietary
components include Widevine, DTCP, a CA manager, and BIST. 319 ELF objects
were inventoried; see `tools/elf_inventory.sh`.

## Storage layout

The disk is **1,000,204,886,016 bytes**, identified in our tooling by size
*and* serial (set `HR54_DISK_SERIAL` — never assume `/dev/sdb`; a transient
host device name is not identity). The MBR identifier is `0x0000103e`. There is
no GPT and no LVM.

| # | Start sector | Size (bytes) | Type | Role |
| --- | --- | --- | --- | --- |
| 1 | 64 | 542,835,712 | XFS? | Boot / vendor data |
| 2 | 1,060,296 | 16,113,320,448 | XFS | **Plugin store — the only writable target** |
| 3 | 32,531,632 | 983,406,247,936 | XFS | `/var` bulk (viewer data) |
| 4 | 1,953,246,960 | 139,829,760 | — | SWDL staging image |

Inter-partition gaps are accounted for in the original report. Filesystem UUIDs
and per-partition geometry are in `docs/ORIGINAL-REPORT.md` §2.

Partition 2 is the target. It carries the plugin chain:

```
/var/network/plugins/7/current          selected asset-7 image
/var/network/plugins/7/7_6933_6840.squashfs
/var/network/plugins/7/7_6933_6840.sig
```

Partition 2 is where `/var/hr54-persist` lives — our durable state directory.

## Boot chain and the trust boundary

```
CFE  (flash, first stage bootloader)
  └─► Linux kernel + initramfs        (flash — TRUSTED, never modified)
        └─► /etc/init.d/rcS
              ├─ brings up network, TFTP
              └─ install_plugin.sh
                    ├─ picks an asset image by version arithmetic
                    ├─ verifies the .sig via sigtst
                    └─ start_mount()  →  mounts it under /opt
                          └─ middleware starts
                                └─ Mp4libFeatureStarter probes
                                      /opt/mp4lib/bin/indexer
                                            └─← WE GET CONTROL HERE
```

The critical fact: **the trusted OS is in flash, but the plugin that runs
arbitrary vendor code is on the writable disk.** A filesystem write to
partition 2 becomes code execution as root at the next feature probe.

Direct answers to the obvious questions:

- **Where does `init` live?** In the flash initramfs, not on the HDD.
- **Can startup scripts on the HDD alter boot?** Not the boot itself — but they
  determine which *plugin* is mounted, and plugins run as root.
- **Are signatures checked?** At the *application* level, in `install_plugin.sh`
  via `sigtst`. The check is what [ROOT.md](ROOT.md) exploits. Hardware secure
  boot is a separate question and is **not** established from the HDD alone.
- **Is there a writable bootloader environment?** No.

### What is NOT trusted

| Tier | Mutability |
| --- | --- |
| Plugin SquashFS | **Mutable via the plugin store** — this is the attack surface |
| BIST USB signature | Verified in hardware |
| `.flash.ec` | Signed update container |
| Live MTD kernel/rootfs | Read-only in practice; never written by this project |
| `/var` on disk | Fully writable — the persistence substrate |

## Acquisition

```sh
HR54_DISK=/dev/sdX HR54_EVIDENCE=./evidence tools/acquire_phase1.sh
```

Read-only identification: `lsblk`, `fdisk`, `parted`, `blkid`, `wipefs -n`,
`sgdisk -p`, three `smartctl` dialects, `hdparm -I`, `blockdev`. Output is
`./evidence/logs/` with a `SHA256SUMS.phase1` manifest.

To image, use `ddrescue`; the original acquisition completed at 100% with zero
errors and the resulting `sdb2.img` SHA-256 is recorded in
[NOT_INCLUDED.md](NOT_INCLUDED.md). Always image partition 2 to a **copy** and
run `xfs_repair` on the copy, never on the original.

## Build note for the beachhead

`linux-port/beachhead/build-hr54d.py` is a **complete MIPS32 big-endian
assembler in Python**. It emits an 812-byte statically-linked, libc-free ELF
root-shell listener. It is self-contained, needs no proprietary input, and is
the most portable thing in this repository — you can regenerate it anywhere
Python runs and check it with `readelf`.

One subtlety it gets right: the `socket()` family argument is `SOCK_DGRAM = 1`
and `SOCK_STREAM = 2` on MIPS, which is the reverse of the x86 ordering that
most people carry in their heads.

```sh
python3 linux-port/beachhead/build-hr54d.py
readelf -h linux-port/beachhead/hr54-d    # ELF 32-bit MSB MIPS, o32, mips32
```

## Related

- [ROOT.md](ROOT.md) — exploiting the plugin store
- [PERSISTENCE.md](PERSISTENCE.md) — surviving reboot
- [ROLLBACK.md](ROLLBACK.md) — undoing everything
