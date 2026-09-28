#!/usr/bin/env python3
"""TFTP client (tftp-hpa 0.40 compatible) to pull files from the HR54 TFTP daemon (UDP/1069)."""
import socket, struct, sys, time, os

BLOCKS = int(os.environ.get('TFTP_BLKSIZE', '65464'))

def rrq(host, port, filename, out, blocksize=BLOCKS):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(20)
    req = struct.pack('!HH', 1, 0) + filename.encode() + b'\0octet\0blksize\0' + str(blocksize).encode() + b'\0'
    s.sendto(req, (host, port))
    total = 0
    negotiated = blocksize
    t0 = time.time()
    last = time.time()
    while True:
        try:
            data, addr = s.recvfrom(70000)
        except socket.timeout:
            sys.stderr.write("timeout after %d bytes\n" % total)
            return False
        op, blk = struct.unpack('!HH', data[:4])
        if op == 6:  # OACK
            negotiated = blocksize
            k = 4
            opts = {}
            while k < len(data):
                e = data.index(b'\0', k)
                key = data[k:e]; k = e + 1
                e = data.index(b'\0', k)
                val = data[k:e]; k = e + 1
                opts[key] = val
            if b'blksize' in opts:
                negotiated = int(opts[b'blksize'])
            sys.stderr.write("OACK %s -> blksize=%d\n" % ({k.decode(): v.decode() for k, v in opts.items()}, negotiated))
            s.sendto(struct.pack('!HH', 4, 0), addr)
            continue
        if op == 5:
            code = struct.unpack('!H', data[4:6])[0]
            sys.stderr.write("tftp error %d %s\n" % (code, data[6:].split(b'\0')[0].decode('latin1')))
            return False
        if op == 3:
            payload = data[4:]
            with open(out, 'ab') as f:
                f.write(payload)
            total += len(payload)
            s.sendto(struct.pack('!HH', 4, blk), addr)
            if len(payload) < negotiated:
                s.close()
                return True
            now = time.time()
            if total % (1 << 20) < negotiated:
                sys.stderr.write("  %.1f MiB %.1f KiB/s\n" % (total / 1048576.0, total / 1024.0 / (now - t0)))
            last = now
        elif op == 4:
            pass

if __name__ == '__main__':
    host = sys.argv[1]; port = int(sys.argv[2]); fn = sys.argv[3]; out = sys.argv[4]
    if os.path.exists(out): os.remove(out)
    ok = rrq(host, port, fn, out)
    sz = os.path.getsize(out) if os.path.exists(out) else 0
    print(("OK " if ok else "FAIL ") + out + " " + str(sz))
    sys.exit(0 if ok else 1)
