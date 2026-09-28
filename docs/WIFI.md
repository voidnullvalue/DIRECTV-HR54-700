# WiFi / NVRAM

The HR54 keeps its saved WiFi profile in a vendor NVRAM object. This is the
reverse engineering of that object, plus the tooling to set **your own** SSID
and passphrase into **your own** receiver's NVRAM.

> **No credential is embedded in this repository, and none may be added.**
> The public tooling takes your SSID and passphrase from the command line,
> stdin, the environment, or a local gitignored file. The original project's
> passphrase is not published — see [NOT_INCLUDED.md](NOT_INCLUDED.md).

## Device and object

| Property | Value |
| --- | --- |
| NVRAM device | **`/dev/nds/nvram0`** — 64 KiB char device, major 203 |
| Manager library | `/opt/nvram/lib/libDtvNVRamMgr.so` |
| WiFi object type | **84** |
| Accessor | **`WifiAccessor`**, GOT link address **`0xC704`** |
| Payload base in image | **`0x3B1D`** |
| Checksum field | **`0x3B19`** |
| Checksum | **32-bit big-endian plain byte sum** (not a CRC) |

`15133 == 0x3B1D` — the offset that `WifiAccessor`'s own disassembly computes
is exactly where the old SSID sits in `/dev/nds/nvram0`. That coincidence is
what confirms the whole chain from accessor to byte offset.

## Field layout

The WiFi object is a flat blob. `WifiAccessor` `memcpy`s fields in and out
through a **3 × 10-byte descriptor table**:

| Offset | Length | Field |
| --- | --- | --- |
| `+0x00` | 32 | **SSID**, NUL-padded |
| `+0x21` | 65 | **WPA passphrase**, NUL-padded |
| `+0x62` | 1 | mode word (`0xfe` observed; mode `1` enables the write path) |

`+0x21` — not `+0x20` — is the passphrase start. Getting this wrong is the
single easiest way to produce an NVRAM image that fails to associate: the
receiver then reads 65 bytes beginning one byte into the passphrase and sees
garbage.

Checksums:

```
size = (meta[12] + 7) & ~7;      # round the object size up to 8
sum  = sum of those `size` bytes
```

Stored big-endian at `0x3B19`.

## How the layout was established

`wifi/nvpre.c` is a `-nostdlib` `LD_PRELOAD` shim, **read-only**, and its header
comment is the full derivation. The essential findings:

- A statically linked musl binary **cannot `dlopen()`** ("Dynamic loading not
  supported"), so a standalone tool cannot bind the vendor library. A *preloaded*
  shared object has no such problem: the box's own `ld-uClibc` resolves undefined
  symbols against libraries the host process already mapped.
- uClibc PIC details: `gp` is a module-wide constant `0x34020`, `.got` sits at
  link-time offset `0x2c030`, and `GOT[-32724]` holds the link-time base
  `0x10000`. The load base comes from `dladdr()`. Both tables are in `.data`
  and built at runtime, so they can only be read **on the box**.
- The **32-byte accessor table** is indexed by `objType` with the accessor at
  `entry+20`. Matching against exported symbols gives `objType 84 -> WifiAccessor`;
  `entry+0` is itself the id `NVGetObject` wants.
- `NVGetObject` rejects `id >= 100` with code 5 and `client >= 9` with code 13 —
  which is why an earlier naive sweep of `100..65535` uniformly returned 5 and
  taught us nothing.
- `NVGetObject` is **gated**: code 3 = read permission denied, code 12 = NVRAM
  semaphore unavailable. Neither is reachable from an arbitrary process such as
  `sleep`, so the object must be read with raw primitives.
- `NVReadBytesFromEEPROM(offset, len, buf)` is the raw reader. **Argument order
  matters**: passing the buffer as the 2nd argument returns 0 without writing,
  which looks exactly like an empty device. This cost time.
- The per-`objType` metadata table (20 bytes/entry) holds EEPROM offset at `+4`,
  stored-checksum offset at `+8`, object size at `+12`, checksum length at `+16`.
- There **is** a write path: when the accessor mode word is 1 it calls
  `NVWriteBytesToEEPROM(offset, size, buf)` on the same offset.

### Build the read-only shim

```sh
ZIG=/path/to/zig LIB=/path/to/dir/containing/libDtvNVRamMgr.so ./build.sh
```

`build.sh` also rewrites the baked `DT_NEEDED` to the **bare library name** —
the vendor library has no SONAME, so an absolute path would not resolve.

```sh
# on the receiver
LD_LIBRARY_PATH=/opt/nvram/lib \
LD_PRELOAD=/var/hr54-transfer/nvpre.so sleep 6
```

`wifi/nvfields.c` is a smaller, libc-free probe that just dumps the descriptor
fields. `wifi/nvtool.c` is the superseded `NVGetObject` name sweeper — kept
because it documents a dead end, not because it works.

## Provisioning your own WiFi

### The easy path: patch a copy on the host

`/dev/nds/nvram0` is a plain char device. `dd` it and diff on a host; no
library needed.

```sh
# 1. BACK UP. Do not skip this.
HR54_HOST=<your-hr54-ip> jellyfin/tools/hr54.sh \
  'dd if=/dev/nds/nvram0 of=/var/hr54-transfer/nvram0.orig.bin bs=64k'

# 2. Pull it to your host (root shell framing, or TFTP).
jellyfin/tools/hr54-pull.sh /var/hr54-transfer/nvram0.orig.bin ./nvram0.orig.bin

# 3. Patch YOUR copy. Credentials come from you, never from this repo.
python3 ../wifi/patch-nvram.py nvram0.orig.bin nvram0.mine.bin \
        "$YOUR_SSID" "$YOUR_PASSPHRASE"

# 4. VERIFY before it goes anywhere near the device.
#    patch-nvram.py prints a byte-level diff and re-derives the checksum;
#    it refuses to write if the layout constants do not match your image.
```

`patch-nvram.py` **re-derives the stored checksum and refuses to patch** if the
image does not match the expected layout. It prints the passphrase **length
only**, never the value.

### Supplying credentials safely

Never bake a credential into a command line that lands in shell history, and
never commit one. All of these are supported:

```sh
# command line (visible in history -- avoid for real credentials)
python3 patch-nvram.py in.bin out.bin "$SSID" "$PASS"

# stdin (preferred)
read -rsp "passphrase: " PASS; echo
python3 patch-nvram.py in.bin out.bin "$SSID" "$PASS"

# environment
SSID=... PASS=... python3 patch-nvram.py in.bin out.bin "$SSID" "$PASS"

# local gitignored config
install -m 600 /dev/null ~/.config/hr54-wifi.conf
printf 'SSID=%s\nPASSPHRASE=%s\n' "$SSID" "$PASS" >> ~/.config/hr54-wifi.conf
set -a; . ~/.config/hr54-wifi.conf; set +a
```

`~/.config/hr54-wifi.conf` and `~/.hr54-sudo-pass` are covered by
[.gitignore](../.gitignore) (`*wifi*.conf`, `*.hr54-pass`). Both are mode `600`.

### Writing it back: guarded, reversible

`wifi/nvwrite.c` is the on-box writer, and it is built to fail closed:

- Built `-nostdlib`; **never prints the credential.**
- Requires an explicit `NVRAM_ACTION` of `apply`, `check`, or `restore`.
  Without it, it writes nothing and says so.
- Before writing anything it re-reads live `/dev/nds/nvram0` and cross-checks
  against the vendor reader (`NVReadBytesFromEEPROM` vs the char device). If
  they disagree, it refuses.
- It computes a full 64 KiB byte diff between staged original and staged
  patched, and **refuses unless every changed byte falls inside the whitelisted
  credential ranges**. Any stray change aborts.
- It validates the staged image structurally: a non-empty NUL-terminated SSID in
  the 32-byte field, a non-empty NUL-terminated passphrase in the 65-byte field,
  and nothing after either terminator. No credential *value* is compiled in.
- After writing it re-reads the full image and re-verifies the vendor checksum.
  **On any failure it automatically restores the original before returning**, so
  the device is never left half-written.
- `NVRAM_ACTION=restore` reverses it, refusing if the live image has diverged
  from both the original and the patched form.

```sh
ZIG=/path/to/zig LIB=/path/to/containing/libDtvNVRamMgr.so ./build-write.sh
```

```sh
# on the receiver
NVRAM_ACTION=check  LD_PRELOAD=/var/hr54-transfer/nvwrite.so sleep 6   # verify only
NVRAM_ACTION=apply LD_PRELOAD=/var/hr54-transfer/nvwrite.so sleep 6   # write
NVRAM_ACTION=restore LD_PRELOAD=/var/hr54-transfer/nvwrite.so sleep 6 # revert
```

### ⚠ Preserve Ethernet during setup

**Keep a working Ethernet connection while you are changing WiFi.** If the
NVRAM write leaves the box without a usable network path, the only way back in
is the physical HDMI/serial/service menu — or a re-image.

On the author's box, `eth0` is the bridge carrying the LAN address and `wl0` is
the wireless client, so Ethernet survived. Do not assume that on yours.

### Rollback

```sh
# 1. restore the original via the guarded path
NVRAM_ACTION=restore LD_PRELOAD=/var/hr54-transfer/nvwrite.so sleep 6

# 2. or, from the host backup, write the original file back
jellyfin/tools/hr54-tftp-put.py nvram0.orig.bin /var/hr54-transfer/ on-receiver
```

Keep `nvram0.orig.bin` until WiFi has been stable for a week. It contains a
credential — store it accordingly.

## Verifying

```sh
# what the box currently has, without touching anything
strings nvram0.mine.bin | head

# byte-level diff, credential-bearing offsets highlighted
cmp -l nvram0.orig.bin nvram0.mine.bin | head -50

# confirm only the intended ranges moved
python3 - <<'EOF'
o=open("nvram0.orig.bin","rb").read(); p=open("nvram0.mine.bin","rb").read()
ranges=[(0x3b19,0x3b1d),(0x3b1d,0x3b3e),(0x3b3e,0x3b4b)]
bad=[i for i in range(len(o)) if o[i]!=p[i]
     and not any(lo<=i<hi for lo,hi in ranges)]
print("out-of-range changed bytes:", bad or "none")
print("stored checksum:", int.from_bytes(p[0x3b19:0x3b1d],"big"))
print("recomputed     :", sum(p[0x3b1d:0x3b1d+128]))
EOF
```

## Do not

- Commit a patched or original NVRAM image. Both contain a credential.
- Write NVRAM on a box you cannot physically reach.
- Assume the offsets hold on a different firmware release. Re-verify with
  `nvfields.c` first; `patch-nvram.py` will refuse if they do not.
- Bypass `NVRAM_ACTION` or the byte-range whitelist. They are the safety net.

## Related

- `wifi/nvpre.c` — the full RE derivation (read the header comment)
- `wifi/nvfields.c` — field-offset probe
- `wifi/patch-nvram.py` — host-side patcher with self-verification
- `wifi/nvwrite.c` — guarded, self-restoring on-box writer
- [NOT_INCLUDED.md](NOT_INCLUDED.md) — which NVRAM artifacts are excluded and why
