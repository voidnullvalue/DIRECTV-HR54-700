# Proprietary inputs

**No DIRECTV firmware, filesystem image, or vendor binary is redistributed in
this repository.** None. What is published is the *transformation*: the
patchers, the build scripts, the offsets, the hashes, and the verification
gates. You supply the inputs from **your own receiver.**

This is both a legal necessity and a practical one — you need the genuine
images anyway, because the exploit depends on their real content.

## Why the genuine image is required

The asset-7 plugin build starts from the genuine signed image extracted from
your own disk, because:

1. The genuine `indexer` must be preserved as `indexer.real` and re-`exec`'d.
2. The genuine `.sig` must be reused **verbatim** — we do not sign anything.
3. The genuine Broadcom `.so`s and firmware blobs are re-packed unchanged.

Fabricating any of that would be forging vendor content. So we do not.

## Inputs you must obtain yourself

### 1. Genuine asset-7 plugin image and signature

| | |
| --- | --- |
| **Filename** | `7_6932_6932.squashfs` and `7_6932_6932.sig` |
| **On your receiver** | `/var/network/plugins/7/` (and `/var/network/plugins/7/current`) |
| **Version note** | asset 7, minimum 6839, stack 6840. Substitute whatever pair your box actually has selected. |
| **How to copy** | see below |
| **Hash** | no public reference — it varies per receiver and per update channel. **Record your own and keep it.** |
| **What we do to it** | `asset7/build-plugin-v2.sh` / `build-plugin-v3.sh` unsquash it, bump the version numbers in 4 XMLs, install our `indexer` wrapper (preserving the genuine binary as `indexer.real`), fix mtimes, and repack with `mksquashfs -comp lzma -b 131072 -exports -all-root`. The `.sig` is **copied verbatim** with only the `IMAGE =` field re-labelled. |
| **Output** | `7_6933_6840.squashfs` + `7_6933_6840.sig` |
| **Why not included** | copyrighted DIRECTV firmware; ~2 MB of vendor Broadcom binaries |

```sh
# pull from the receiver (root shell framing)
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'cp /var/network/plugins/7/7_6932_6932.squashfs /var/hr54-transfer/
   cp /var/network/plugins/7/7_6932_6932.sig     /var/hr54-transfer/'

jellyfin/tools/hr54-pull.sh /var/hr54-transfer/7_6932_6932.squashfs ./
jellyfin/tools/hr54-pull.sh /var/hr54-transfer/7_6932_6932.sig ./
sha256sum 7_6932_6932.squashfs 7_6932_6932.sig   # record these
```

### 2. `sigtst` (signature verifier)

| | |
| --- | --- |
| **Path on receiver** | `/opt/sig/bin/sigtst` |
| **How to copy** | `hr54-pull.sh /opt/sig/bin/sigtst ./` — or use it on the box, which is better |
| **What we do to it** | nothing. We *run* it. `asset7/verify-offline-v3.sh` uses the genuine verifier to prove our `.sig` redirect is accepted. |
| **Why not included** | vendor binary |
| **Alternative** | `tools/plugin_verify.py` re-implements the DSA verification in Python, but needs the three candidate key offsets that were recovered from `sigtst` — supply those via `--sigtst` or by hand |

### 3. `libDtvNVRamMgr.so` (WiFi NVRAM manager)

| | |
| --- | --- |
| **Path on receiver** | `/opt/nvram/lib/libDtvNVRamMgr.so` |
| **How to copy** | `hr54-pull.sh /opt/nvram/lib/libDtvNVRamMgr.so ./` |
| **What we do to it** | nothing. `wifi/build.sh` links against it and rewrites the baked `DT_NEEDED` to the **bare filename** (it has no SONAME). |
| **Why not included** | vendor shared library |

```sh
ZIG=/path/to/zig LIB=$(pwd) ./wifi/build.sh
```

### 4. The stock Settings FlatBuffer

| | |
| --- | --- |
| **Path on receiver** | the `STB_Settings` resource under the UI assets |
| **What we do to it** | `jellyfin/ui/inspect_settings.py` reads it; `add_settings_row.py` and `rename_help_tile.py` produce `STB_Settings.jellyfin.fb` / `.tv.fb` overlays |
| **Why not included** | vendor UI resource |
| **Note** | the overlay *mechanism* works, but the compiled `createMenuRowMap` whitelists seven stock sections, so an arbitrary new row never renders. See [ITV-WEBKIT.md](ITV-WEBKIT.md). |

### 5. `/opt/dtv/dtv.car` and the sdb4 rootfs

| | |
| --- | --- |
| **What** | the CEEJ VM bytecode archive containing the DVR/middleware logic, and the full recovered rootfs |
| **Why not included** | copyrighted vendor software, including `dtv.car` (~20 MB), `libdvr.so` (~12 MB), `libcwebkit.so` (~14 MB), `europa.ko`, and the rest |
| **What we publish instead** | the *analysis* — see [DEAD-ENDS.md](DEAD-ENDS.md) and `docs/ORIGINAL-REPORT.md` |
| **We do not modify it** | patching CEEJ bytecode risks crashing `siege`; the plan was never executed |

### 6. Full disk images and NVRAM

| | |
| --- | --- |
| **sdb2.img** (15 GiB) | the authoritative partition-2 restore source. SHA-256 `1b93be57da0aa5a07eb54bbf900a3bb6d778e0c1a82aee4472223e304737c0ad`. **Not included** — it is a verbatim copy of the owner's disk. Take your own with `ddrescue`. |
| **sdb1/3/4.img** | never written by this project; no need for your own either |
| **`nvram0` images** | **never publish these — they contain a WiFi credential.** Generate yours with `dd if=/dev/nds/nvram0`. See [WIFI.md](WIFI.md). |
| **sdb3-rt-placeholder.img** | a **sparse** 983,406,247,936-byte all-zero file that satisfies XFS geometry when mounting partition 2 read-only. **Regenerate, do not download:** `truncate -s 983406247936 sdb3-rt-placeholder.img` |

### 7. Access card and account data

The original analysis recorded DIRECTV access-card IDs and a receiver account
ID. **Both are redacted here.** They are account-linked identifiers, not
technical facts, and nothing in the tooling needs them. The card-*reader* error
analysis that referenced them is preserved in `userland-mods/playback/FINDINGS.md`
with the identifiers replaced by `CARD_ID_REDACTED` / `RECEIVER_ID_REDACTED`.

### 8. Receiver `root` password hash

Also redacted. It is a real credential artifact recovered from the device's
`/etc/shadow`, it resisted a stock dictionary, and publishing it would only
help someone else crack it. `tools/crack_root_hash.pl` is kept and now takes
the hash from **your** device, from outside the repository.

## What you do NOT need

- A cross-compiler for the beachhead. `build-hr54d.py` is a self-contained
  Python MIPS assembler.
- `zig`, unless you are building `hr54-jf` or the NVRAM tools. For `hr54-jf`,
  any `mips-linux-musleabi` compiler works **as long as you pin `-mcpu=mips32`.**
- The development host. The appliance is receiver-native.

## Summary

| Input | Source | In repo? |
| --- | --- | --- |
| asset-7 genuine image + `.sig` | your `/var/network/plugins/7/` | ✗ proprietary |
| `sigtst` | your `/opt/sig/bin/` | ✗ proprietary |
| `libDtvNVRamMgr.so` | your `/opt/nvram/lib/` | ✗ proprietary |
| `STB_Settings` FlatBuffer | your UI assets | ✗ proprietary |
| `dtv.car`, sdb4 rootfs | your disk | ✗ proprietary |
| `sdb2.img` restore image | `ddrescue` your own disk | ✗ verbatim owner data |
| `nvram0` images | `dd` your own device | ✗ **contains a credential** |
| sdb3 placeholder | `truncate -s 983406247936` | ✗ 916 GiB, trivially generated |
| `indexer.real` | inside the genuine image | ✗ proprietary |
| **Every patcher, builder, verifier, and the whole appliance source** | this repo | ✓ |

## Related

- [NOT_INCLUDED.md](NOT_INCLUDED.md) — the complete exclusion list, with
  regeneration steps for everything
- [ROOT.md](ROOT.md) — the mechanism, and the non-forgery note
- [REPRODUCTION.md](REPRODUCTION.md) — the order to do it in
