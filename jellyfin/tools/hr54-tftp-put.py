#!/usr/bin/env python3
"""Minimal TFTP (RFC 1350) writer used to stage files on the HR54.

The persistent asset-7 launcher runs in.tftpd on UDP/1069 with /var/hr54-transfer
as its served directory, which is the only practical bulk transfer path: the
receiver has no base64, python, perl or openssl, and its BusyBox hexdump has no
-r, so pushing a multi-megabyte image through the root shell is impractical.

Write flow: client sends WRQ, the server answers ACK 0, then the client sends
DATA 1, 2, 3 ... and the server acknowledges each block. The last DATA block is
the short one, after which the transfer is complete.
"""

import argparse
import os
import socket
import sys
import time

BLOCK = 512
WRQ, DATA, ACK, ERROR, OACK = 2, 3, 4, 5, 6


class TftpError(RuntimeError):
    pass


def put(host, port, local, remote, timeout=6.0, retries=10):
    payload = open(local, "rb").read()
    total = len(payload)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    peer = (host, port)
    peer_addr = None

    def await_reply(expect, block, resend):
        """Wait for one packet; resend on timeout. Returns the accepted block."""
        nonlocal peer_addr
        deadline = time.time() + retries * timeout
        attempt = 0
        while time.time() < deadline:
            try:
                data, sender = sock.recvfrom(BLOCK + 4)
            except socket.timeout:
                attempt += 1
                if attempt > retries:
                    raise TftpError("timeout waiting for %s %d" % (expect, block))
                resend()
                continue
            peer_addr = sender
            if len(data) < 2:
                continue
            opcode = int.from_bytes(data[:2], "big")
            if opcode == ERROR:
                code = int.from_bytes(data[2:4], "big")
                raise TftpError("TFTP error %d: %s"
                                % (code, data[4:].split(b"\0")[0].decode("latin1")))
            if opcode == OACK:
                continue
            if opcode != expect:
                continue
            got = int.from_bytes(data[2:4], "big")
            if got != block:
                # Duplicate or stale: acknowledge it again and keep waiting.
                sock.sendto(ACK.to_bytes(2, "big") + got.to_bytes(2, "big"), sender)
                continue
            return got
        raise TftpError("gave up on %s %d" % (expect, block))

    def ack(n):
        sock.sendto(ACK.to_bytes(2, "big") + n.to_bytes(2, "big"), peer_addr)

    request = WRQ.to_bytes(2, "big") + remote.encode() + b"\0octet\0"
    sock.sendto(request, peer)
    await_reply(ACK, 0, lambda: sock.sendto(request, peer))

    sent = 0
    block = 0
    while sent < total:
        block += 1
        chunk = payload[sent:sent + BLOCK]
        packet = DATA.to_bytes(2, "big") + block.to_bytes(2, "big") + chunk
        sock.sendto(packet, peer_addr)
        try:
            await_reply(ACK, block, lambda p=packet, pa=peer_addr: sock.sendto(p, pa))
        except TftpError:
            raise
        sent += len(chunk)
    return block, sent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("local")
    parser.add_argument("remote")
    parser.add_argument("--port", type=int, default=1069)
    args = parser.parse_args()
    size = os.path.getsize(args.local)
    start = time.time()
    blocks, sent = put(args.host, args.port, args.local, args.remote)
    took = max(time.time() - start, 0.001)
    print("wrote %s -> %s:%d/%s  %d blocks, %d bytes in %.1fs (%.0f KiB/s)"
          % (args.local, args.host, args.port, args.remote, blocks, sent, took,
             sent / 1024.0 / took))
    if sent != size:
        print("WARNING: sent %d of %d bytes" % (sent, size), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
