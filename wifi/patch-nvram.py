#!/usr/bin/env python3
"""Patch the HR54's saved WiFi profile inside a /dev/nds/nvram0 image.

This is a host-side tool: it edits a *copy* of the NVRAM image and never touches
the receiver.  Feeding the result back to the box is a separate, deliberate step
documented in jellyfin/HANDOFF.md.

Why a byte-level patch rather than the vendor API
-------------------------------------------------
libDtvNVRamMgr.so exposes NVGetObject/NVSetObject, but both are gated: an
unprivileged process gets code 3 (read permission denied) or code 12 (cannot
take the NVRAM semaphore), so the object cannot be read or written that way from
an ordinary process.  The raw primitives have no such gate, and reverse
engineering the stripped library pinned the layout exactly:

  * The accessor table is indexed by objType and its entry+20 is the accessor
    function.  Matching that against the exported symbols gives objType 84 ->
    WifiAccessor (symbol value 50948 == 0xC704).  NVGetObject's id for it is
    84 (0x54); NVGetObject rejects ids >= 100 with code 5, which is why sweeping
    100..65535 finds nothing.
  * WifiAccessor computes the object's EEPROM offset as
    (u16_from_table + 15133) & 0xffff.  15133 == 0x3B1D, and that is exactly
    where the old SSID sits in the image, so the two agree.
  * The object is a flat blob.  The accessor memcpys fields between it and a
    caller struct using a 3 x 10-byte descriptor table; from the image:

        +0x00  SSID, 32-byte field, NUL-padded
        +0x21  WPA passphrase, 65-byte field; old value is 12 chars + NUL
        +0x62  0xfe   (security/mode field, left alone)

    A read-only probe of WifiAccessor's three 10-byte descriptors on the live
    receiver established field 0 at +0x00 (33 bytes), field 1 at +0x21
    (65 bytes), and field 2 at +0x62.  The previous +0x22 interpretation
    accidentally preserved the old credential's first byte, which made the
    receiver use the wrong password.  The passphrase must start at +0x21 and
    fit with its NUL inside the 65-byte field.
  * The 32-bit checksum lives in the 4 bytes immediately before the object, at
    0x3B19, and is a plain byte sum of the object - not a CRC.  It is stored
    big-endian: the receiver is a big-endian MIPS box (readelf reports
    "2's complement, big endian"), so the stored bytes 00 00 08 51 are read as
    0x00000851, matching CalcChecksumEEPROM.

        size = (meta[12] + 7) & ~7
        sum  = 0
        for i in range(size): sum += object[i]

    Verified two ways on the box: the stored field reads 0x851 and the library's
    own CalcChecksumEEPROM(84) also returns 0x851.  The sum is unchanged for any
    object length from 99 to 255 bytes (the tail is zero padding), which is how
    the real length is bounded even though the metadata table is runtime-only.
  * This deliberately changes only the SSID field, the passphrase and the
    checksum.  The mode byte at +0x62 is carried over untouched.  The old
    profile was WPA2-AES and the replacement AP advertises WPA2-PSK-CCMP.

usage:
  patch-nvram.py <nvram0.bin> <out.bin> [ssid] [passphrase]

Credentials are NEVER hardcoded here. They are resolved, in order, from:

  1. positional arguments        patch-nvram.py in.bin out.bin "$SSID" "$PASS"
  2. environment                 SSID=... PASSPHRASE=... patch-nvram.py in.bin out.bin
  3. stdin, one value per line    printf '%s\\n%s\\n' "$SSID" "$PASS" | patch-nvram.py in.bin out.bin
  4. a local config file          patch-nvram.py in.bin out.bin --config ~/.config/hr54-wifi.conf
                                 (KEY=VALUE, mode 600, and gitignored)

Prefer stdin or a private config file: a passphrase on the command line lands in
shell history. In every case the passphrase is read from stdin and never echoed
back; the report prints its length only.
"""
import os
import struct
import sys

# Offsets in the EEPROM image, all confirmed against the receiver.
CSUM_OFF = 0x3B19
OBJ_OFF = 0x3B1D
SSID_OFF = OBJ_OFF + 0x00
SSID_LEN = 32
PASS_OFF = OBJ_OFF + 0x21
PASS_LEN = 65

# The object is zero-padded out to at least here; the byte sum is identical for
# any length in [99, 255], so 128 is a safe representative.
SUM_LEN = 128


def fail(msg):
    sys.stderr.write("patch-nvram: %s\n" % msg)
    raise SystemExit(1)


def from_config(path):
    """Read SSID=/PASSPHRASE= from a mode-600 local file. Returns (ssid, pass)."""
    try:
        st = os.stat(path)
    except OSError as e:
        fail("cannot read config %s: %s" % (path, e))
    if st.st_mode & 0o077:
        fail("refusing to read %s: mode %o is group/world accessible, use 600"
             % (path, st.st_mode & 0o777))
    vals = {}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, _, v = line.partition("=")
            vals[k.strip().upper()] = v.strip().strip('"').strip("'")
    return vals.get("SSID"), vals.get("PASSPHRASE")


def resolve_credentials(argv):
    """argv has had the in/out paths removed. Returns (ssid, passphrase)."""
    # --config FORM
    if len(argv) >= 1 and argv[0] == "--config":
        if len(argv) < 2:
            fail("--config needs a path")
        return from_config(os.path.expanduser(argv[1]))

    # 1. positional
    if len(argv) >= 2:
        return argv[0], argv[1]
    if len(argv) == 1:
        # SSID given, passphrase from stdin.
        return argv[0], read_stdin(need=1, what="passphrase")

    # 2. environment
    env_ssid = os.environ.get("SSID") or os.environ.get("HR54_SSID")
    env_pass = os.environ.get("PASSPHRASE") or os.environ.get("HR54_PASSPHRASE")
    if env_ssid is not None and env_pass is not None:
        return env_ssid, env_pass

    # 3. stdin: two lines
    lines = read_stdin(need=2, what="ssid and passphrase")
    return lines[0], lines[1]


def read_stdin(need, what):
    """Read `need` line(s) from stdin without echoing. Returns list of str."""
    if sys.stdin.isatty():
        # Avoid a password sitting visibly on the terminal.
        import getpass
        out = []
        for _ in range(need):
            out.append(getpass.getpass("patch-nvram: %s: " % what))
        return out
    lines = []
    for line in sys.stdin:
        line = line.rstrip("\n")
        if line == "" and len(lines) >= need:
            break
        lines.append(line)
        if len(lines) == need:
            break
    if len(lines) < need:
        fail("expected %d line(s) on stdin (%s)" % (need, what))
    return lines


def main(argv):
    args = argv[1:]
    if "--help" in args or "-h" in args:
        sys.stdout.write(__doc__)
        return 0
    if len(args) < 2:
        fail("usage: patch-nvram.py <nvram0.bin> <out.bin> [ssid] [passphrase]\n"
             "       see --help for credential sources")
    src, dst = args[0], args[1]
    ssid, passphrase = resolve_credentials(args[2:])
    if not ssid or not passphrase:
        fail("no SSID/passphrase given (argv, SSID/PASSPHRASE env, stdin, or --config)")

    ssid_b = ssid.encode("ascii", "replace")
    pass_b = passphrase.encode("ascii", "replace")
    if not 1 <= len(ssid_b) <= SSID_LEN:
        fail("SSID must be 1..%d bytes, got %d" % (SSID_LEN, len(ssid_b)))
    if not 1 <= len(pass_b) < PASS_LEN:
        # Longer passphrases would need the neighbouring field moved, which is
        # a different (and riskier) edit; refuse rather than corrupt the object.
        fail("passphrase must be 1..%d bytes to fit the existing field, got %d"
             % (PASS_LEN - 1, len(pass_b)))

    original = open(src, "rb").read()
    image = bytearray(original)
    if len(image) < OBJ_OFF + SUM_LEN:
        fail("%s is too small (%d bytes) to be an NVRAM image" % (src, len(image)))

    old_ssid = bytes(image[SSID_OFF:SSID_OFF + SSID_LEN]).split(b"\0")[0]
    old_pass = bytes(image[PASS_OFF:PASS_OFF + PASS_LEN]).split(b"\0")[0]
    old_csum, = struct.unpack_from(">I", image, CSUM_OFF)

    # Sanity-check our understanding before changing anything: re-derive the
    # stored checksum from the object we are about to edit.
    derived = sum(image[OBJ_OFF:OBJ_OFF + SUM_LEN]) & 0xFFFFFFFF
    if derived != old_csum:
        fail("checksum self-check failed: stored 0x%08x, computed 0x%08x.\n"
             "       Refusing to patch: the layout constants in this script do\n"
             "       not match this image." % (old_csum, derived))

    # NUL-terminated inside the existing field, zero padding preserved.
    image[SSID_OFF:SSID_OFF + SSID_LEN] = ssid_b.ljust(SSID_LEN, b"\0")
    image[PASS_OFF:PASS_OFF + PASS_LEN] = pass_b.ljust(PASS_LEN, b"\0")

    new_csum = sum(image[OBJ_OFF:OBJ_OFF + SUM_LEN]) & 0xFFFFFFFF
    struct.pack_into(">I", image, CSUM_OFF, new_csum)

    open(dst, "wb").write(image)

    changed = [i for i in range(len(image)) if image[i] != original[i]]
    print("patched %s -> %s" % (src, dst))
    print("  SSID        %r -> %r" % (old_ssid.decode("latin1"), ssid))
    print("  passphrase  %d bytes -> %d bytes (value not printed)"
          % (len(old_pass), len(pass_b)))
    print("  checksum    0x%08x -> 0x%08x  (at 0x%04x)"
          % (old_csum, new_csum, CSUM_OFF))
    print("  %d bytes differ overall:" % len(changed))
    for i in changed:
        print("    0x%04x: 0x%02x -> 0x%02x" % (i, original[i], image[i]))
    print("\nNot applied to the receiver.  See docs/WIFI.md for the guarded write step (NVRAM_ACTION=apply/restore).")


if __name__ == "__main__":
    main(sys.argv)
