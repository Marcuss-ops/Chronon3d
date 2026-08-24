#!/usr/bin/env python3
"""Exercise the daemon transport with malformed FlatBuffers frames.

The one-byte payload is intentionally not a valid IPC message; the transport
frame itself is valid and the daemon must return a structured error reply for
each request without leaking a client connection or resource.
"""
from __future__ import annotations

import argparse
import socket
import struct


def receive_exact(sock: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise RuntimeError("daemon closed the socket before the reply")
        result.extend(chunk)
    return bytes(result)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("socket_path")
    parser.add_argument("--requests", type=int, default=1000)
    args = parser.parse_args()
    if args.requests <= 0:
        raise SystemExit("--requests must be positive")

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(10.0)
        sock.connect(args.socket_path)
        for _ in range(args.requests):
            payload = b"\x00"
            sock.sendall(struct.pack(">I", len(payload)) + payload)
            reply_size = struct.unpack(">I", receive_exact(sock, 4))[0]
            if reply_size == 0 or reply_size > 64 * 1024 * 1024:
                raise RuntimeError(f"invalid reply length: {reply_size}")
            receive_exact(sock, reply_size)
    print(f"IPC_RESOURCE_WORKLOAD_PASS: requests={args.requests}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
