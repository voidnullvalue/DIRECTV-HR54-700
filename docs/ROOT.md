# Root

## The one thing to understand first

**We do not forge a DIRECTV signature. We do not need one.**

This is the single most important clarification in this project, and it is
stated here first because a reader skimming for "signature bypass" will
otherwise draw the wrong conclusion and the wrong risk assessment.

What actually happens:

1. A genuine, correctly signed asset image exists on the receiver at
   `/var/network/plugins/7/7_6932_6932.squashfs` with a matching `.sig`.
2. We write **our own** bytes over that filename. The `.sig` file is left
   exactly as DIRECTV shipped it and is never modified.
3. The genuine DSA signature therefore still **verifies correctly** — because
   `sigtst` hashes the file at the path named in the `.sig`'s `IMAGE` field,
   and we replaced the contents at that path.
4. Independently, the loader selects and mounts the image by a *different*
   mechanism: a filename/version arithmetic path that resolves to a name we
   control.

The signature check passes. The code that runs is ours. The signature does not
cover the code that runs.

> This is a **path/filename confusion** and a **confused-deputy** bug, not a
> cryptographic break. The DSA/MD5 cryptography is irrelevant to it. We do not
> possess, use, or need a signing key, and nothing in this repository can
> produce a signature DIRECTV did not already produce.

The genuine signature is *reused*, not *recomputed*. `asset7/build-plugin-v2.sh`
and `build-plugin-v3.sh` copy the `SIGNATURE =` line out of the genuine `.sig`
verbatim and only re-label the `IMAGE =` field — and they deliberately point
that `IMAGE` field at the **genuine** path. This is exactly why the modified
payload boots. The full treatment is in
[ORIGINAL-REPORT.md](ORIGINAL-REPORT.md) §15.1.

## The vulnerability, precisely

`install_plugin.sh` does two things that should have been one thing:

```sh
# verify_plugin_sig()  — /etc/init.d/rcS:453, install_plugin.sh:237-255
#   hashes the file NAMED IN the .sig  (IMAGE = <path>)
#   and checks it against SIGNATURE = <hex>

# start_mount()        — install_plugin.sh:1248-1324
#   mounts a DIFFERENT file, derived from the ASSET NAME
```

The `.sig` file is plaintext:

```
IMAGE = /var/network/plugins/7/7_6932_6932.squashfs
SIGNATURE = <80 hex chars, SHA-1 + 1024/160-bit DSS>
SIZE = <bytes>
```

None of `IMAGE`, `SIGNATURE`, or `SIZE` is itself authenticated. The verifier
is asked about one file; the loader acts on another. Replace the contents at
the named path and both actors are satisfied.

Asset selection uses version arithmetic (asset 7 minimum 6839, stack 6840),
which is why `6933`/`6840` naming matters. The post-boot directory
`/var/network/plugins/7/current` is the decisive evidence that the selection
resolves to our substituted file.

## From vulnerability to code execution

`Mp4libFeatureStarter` runs an availability probe that **executes**
`/opt/mp4lib/bin/indexer`. If the mounted plugin image has a shell script at
that path, the probe executes it as root. There is no signature re-check at
execution time, because the signature was already "verified" against a
different file.

That is the entire beachhead: root, at a predictable point in boot, before the
middleware finishes starting.

### A wrong turn worth knowing about

The first attempt assumed the trigger was `libmp4lib.so` / `Mp4libFeatureStarter`
loading a patched library. It was not. The real path is a **boot-time probe
that executes `/opt/mp4lib/bin/indexer`**. Attacking the shared library would
have produced a binary that never ran.

## The wrapper

`linux-port/beachhead/indexer` is installed as `mp4lib/bin/indexer`. It:

```sh
#!/bin/sh
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/opt/mp4lib/bin
export PATH
umask 077

# First action: a shell builtin and a redirection. No dependency on /tmp,
# on any external utility, or on anything that could be missing.
printf 'HR54_ROOT_EXEC\n' > /var/HR54_ROOT_PROOF

# Offline/QEMU validation branch -- shell builtins only, because qemu-user
# cannot transparently exec a second target-architecture program without
# host binfmt support.
if [ "${HR54_OFFLINE_TEST:-}" = 1 ]; then
    printf 'HR54_BEACHHEAD_LAUNCHER_OK\n' > /var/HR54_BEACHHEAD_TEST
    exit 22
fi

/bin/sync

# mkdir is an atomic RAM-backed once-per-boot guard: repeated feature probes
# still delegate to the genuine indexer but do not duplicate firewall rules
# or listeners.
if /bin/mkdir /tmp/hr54-beachhead.starting 2>/dev/null; then
    /bin/mkdir -p /var/hr54-beachhead /var/hr54-transfer /var/hr54-inventory
    /bin/chmod 700 /var/hr54-beachhead /var/hr54-transfer /var/hr54-inventory

    /opt/mp4lib/bin/collect-inventory >>/var/hr54-beachhead/inventory-launch.log 2>&1 &
    # ... install restrict_port for tcp/5777 and udp/1069 ...
    /opt/mp4lib/bin/hr54d &
    in.tftpd &

    # Finally: hand control back to the genuine vendor binary.
    exec /opt/mp4lib/bin/indexer.real "$@"
fi

exec /opt/mp4lib/bin/indexer.real "$@"
```

Three design points worth stealing:

- **The proof is the first statement in the file.** `printf` and `>` are shell
  builtins. It cannot fail for environmental reasons.
- **`indexer.real` is preserved and exec'd.** Vendor behaviour is unchanged.
  `verify-offline-v3.sh` proves the preserved binary still returns 22 and
  prints its banner.
- **The once-per-boot guard is `mkdir`.** Atomic, no state file to corrupt, and
  it resets naturally on reboot.

## Root shell

`linux-port/beachhead/build-hr54d.py` emits `hr54d`: an 812-byte static,
libc-free ELF listening on **TCP/5777**, and on accept doing
`socket` → `setsockopt` → `bind 0.0.0.0:5777` → `listen(8)` → `accept` →
`fork` → `dup2`×3 → `execve("/bin/bash", ["-i"])`.

It is generated by a hand-written MIPS32 big-endian assembler in Python. No
cross-toolchain needed.

> ### ⚠ There is no authentication
>
> **Any host that can reach TCP/5777 on your receiver gets a root shell.** No
> password, no key, no application-layer check of any kind.
>
> The wrapper installs a `restrict_port` rule: default DROP, then ACCEPT for
> `127/8`, `10/8`, `172.16/12`, `192.168/16`, `169.254/16`. That is the *only*
> thing standing between your LAN and a root shell on your DVR.
>
> Run this only on a trusted private LAN you control. If you must expose the
> receiver, tunnel it. Never port-forward 5777.

UDP/1069 (`tftp-hpa`, running unprivileged as `nobody` on every boot, serving a
mode-0666 directory) is a second arbitrary-file-write primitive, and is used as
the bulk upload path. See [PROPRIETARY_INPUTS.md](PROPRIETARY_INPUTS.md).

## Offline verification gate

Never write to the disk before verifying the payload offline. The scripts in
`asset7/` do this rigorously:

| Script | What it proves |
| --- | --- |
| `verify-offline.sh` | v1: genuine `sigtst` accepts the redirect; payload extracts and runs |
| `verify-offline-v2.sh` | v2: corrected image, full `sigtst` + ELF + mode-0700, runs under `bwrap` |
| `verify-offline-v3.sh` | v3: all shell scripts byte-match, `sh -n` clean, mode 0700, launcher **and the preserved `indexer.real`** both behave correctly |

`verify-offline-v3.sh` is the one to use. Its final section separately
executes `indexer.real` and asserts it still returns 22 and still prints its
banner — proving we did not damage vendor functionality.

`tools/plugin_verify.py` re-implements the DSA verification independently in
Python (SHA-1 + 1024/160-bit DSS, little-endian r/s, big-endian hash) so a
third party can confirm the result **without trusting the vendor binary**. It
does require the genuine `sigtst` to have supplied the three candidate key
offsets.

## Install discipline

`asset7/install-to-sdb2.sh` and `asset7/install-via-mips-vm.sh` are
deliberately paranoid, and the discipline is worth copying even if you never
run them:

- Identify the disk by **size and serial**, never by `/dev/sdX` name.
- Assert the write guard is still set **immediately before every write**.
- Refuse unless the final line is `OFFLINE_VERIFY_V3=PASS`.
- Refuse unless the operator passed the literal
  `I_UNDERSTAND_ONLY_SDB2` / `I_UNDERSTAND_ONLY_CONFIRMED_PARTITION_2`.
- **Never** touch sdb1, sdb3, sdb4, or the partition table.
- Return the disk read-only on exit, via a `trap`.

`install-via-mips-vm.sh` goes further: the write is performed by a
**big-endian MIPS QEMU Malta guest** with only `sdb2` exposed, so the write is
architecturally honest. Only one drive is visible to the guest, so a bug
cannot reach another disk.

**Rollback is mandatory and comes first.** See [ROLLBACK.md](ROLLBACK.md).
See also [PROPRIETARY_INPUTS.md](PROPRIETARY_INPUTS.md) for what you must
obtain from your own receiver.

## What this project does not do

- Does not write flash, MTD, or partition 4.
- Does not modify CFE.
- Does not patch `/opt/dtv/dtv.car` (it is a CEEJ VM bytecode archive; patching
  it risks crashing `siege` — see [DEAD-ENDS.md](DEAD-ENDS.md)).
- Does not alter tuner or subscriber authorization state.
- Does not perform any content-protection bypass. Paid DIRECTV content remains
  protected; this project plays *your own* Jellyfin library.

## Related

- [PERSISTENCE.md](PERSISTENCE.md) — making it survive reboot
- [HARDWARE.md](HARDWARE.md) — platform and storage layout
- [DEAD-ENDS.md](DEAD-ENDS.md) — kexec, Dirty COW, and other closed doors
- [ORIGINAL-REPORT.md](ORIGINAL-REPORT.md) — the full security analysis
