#!/usr/bin/env python3
"""A throwaway loopback WebSocket server that replays a committed trace.

**This is the harness `wsclient.py`'s header has claimed since M4 stage 0, and
until M5 stage 0's review it did not exist in the repository.** The header said
the extraction was verified "by replaying a committed trace at both versions",
and the script that did it lived on one machine, in a scratchpad, so nobody else
could reproduce the claim. A verification method that is not in the tree is a
verification nobody but its author can run -- which is the same defect as an
unpinned figure, one level up.

Why a loopback server is the right shape. A capture is a live-network sample, so
two runs against a venue can never be compared byte for byte: the frames differ.
The only way to hold the frames still is to serve KNOWN ones, and the only frames
this project trusts are the committed traces. So: read a trace, serve its frames
verbatim over `ws://127.0.0.1`, and let a capture tool record them.

Two uses:

  * **Byte-identity across a change to `wsclient.py`.** Run a capture tool before
    and after, normalise the two fields a replay cannot hold still (`rx_ns`, a
    clock, and `captured_at`, a date), and diff. Anything else that moves is the
    change moving it.
  * **Exercising a capture tool at all** (`capture_binance.py --selfcheck`),
    including the paths no live capture reaches on demand: a server PING, a
    server-initiated CLOSE, and a REST fetch that fails.

`--ping-after N` sends a control frame after N text frames, which is the case no
committed trace contained before M5 stage 0 and the one that closes a socket when
it is answered wrongly.

Python 3 stdlib only. Lives in tools/.
"""
from __future__ import annotations

import base64
import hashlib
import itertools
import socket
import struct
import sys
import threading
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_TEXT = 0x1
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA


def frames_of(path: str) -> list[str]:
    """The verbatim frame text of every record in a committed trace.

    Reads the file directly rather than through `tracefile.read_capture`, and
    deliberately so: this server must be able to replay a trace even when the
    reader is the thing under test.
    """
    out: list[str] = []
    with open(path, encoding="utf-8") as fh:
        fh.readline()                       # the metadata header
        for line in fh:
            line = line.strip()
            if not line:
                continue
            if '"kind": "' in line.split('"frame": ')[0]:
                continue        # a REST body or control record is NOT a WS frame
            at = line.find('"frame": ')
            if at < 0:
                continue
            raw = line[at + len('"frame": '):].rstrip()
            if raw.endswith("}"):
                raw = raw[:-1]
            if raw != "null":               # control records carry no frame
                out.append(raw)
    return out


def _send(sock: socket.socket, opcode: int, payload: bytes = b"") -> None:
    """Send one unmasked server frame. Servers must not mask (RFC 6455)."""
    header = bytearray([0x80 | opcode])
    n = len(payload)
    if n < 126:
        header.append(n)
    elif n < 65536:
        header.append(126)
        header += struct.pack(">H", n)
    else:
        header.append(127)
        header += struct.pack(">Q", n)
    sock.sendall(bytes(header) + payload)


def _drain_client(sock: socket.socket, seen: list) -> None:
    """Collect client->server frames so a pong can be OBSERVED, not assumed."""
    buf = bytearray()
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                return
            buf.extend(chunk)
            while len(buf) >= 2:
                b1 = buf[1]
                opcode = buf[0] & 0x0F
                masked = b1 & 0x80
                length = b1 & 0x7F
                off = 2
                if length == 126:
                    if len(buf) < off + 2:
                        break
                    length = struct.unpack_from(">H", buf, off)[0]
                    off += 2
                elif length == 127:
                    if len(buf) < off + 8:
                        break
                    length = struct.unpack_from(">Q", buf, off)[0]
                    off += 8
                key = b""
                if masked:
                    if len(buf) < off + 4:
                        break
                    key = bytes(buf[off:off + 4])
                    off += 4
                if len(buf) < off + length:
                    break
                payload = bytes(buf[off:off + length])
                if masked:
                    payload = bytes(a ^ b for a, b in
                                    zip(payload, itertools.cycle(key)))
                del buf[:off + length]
                seen.append((opcode, payload))
    except OSError:
        return


def serve_once(frames: list[str], port: int, *, ping_after: int = 0,
               ping_payload: bytes = b"dc-ping", gap_s: float = 0.0015,
               linger_s: float = 0.5) -> list:
    """Accept ONE connection, replay `frames`, close. Returns what the client sent.

    Single-connection by design: a capture under test should not be able to mask
    a failure by silently reconnecting.
    """
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    try:
        conn, _ = srv.accept()
    finally:
        srv.close()

    buf = b""
    while b"\r\n\r\n" not in buf:
        more = conn.recv(4096)
        if not more:
            conn.close()
            return []
        buf += more
    key = ""
    for line in buf.decode("iso-8859-1").split("\r\n"):
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
    accept = base64.b64encode(
        hashlib.sha1((key + GUID).encode()).digest()).decode()
    conn.sendall(("HTTP/1.1 101 Switching Protocols\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())

    seen: list = []
    threading.Thread(target=_drain_client, args=(conn, seen), daemon=True).start()
    try:
        for i, frame in enumerate(frames):
            _send(conn, OP_TEXT, frame.encode("utf-8"))
            if ping_after and i == ping_after:
                _send(conn, OP_PING, ping_payload)
            time.sleep(gap_s)
        time.sleep(linger_s)
        _send(conn, OP_CLOSE, struct.pack(">H", 1000))
    except OSError:
        pass
    time.sleep(0.2)
    conn.close()
    return seen


def pongs(seen: list) -> list[bytes]:
    """The payloads the client sent back as pongs."""
    return [payload for opcode, payload in seen if opcode == OP_PONG]


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if len(argv) < 2:
        sys.stderr.write(
            "usage: ws_loopback.py <trace.ndjson> <port> [--ping-after N]\n")
        return 2
    trace, port = argv[0], int(argv[1])
    ping_after = 0
    if "--ping-after" in argv:
        ping_after = int(argv[argv.index("--ping-after") + 1])
    frames = frames_of(trace)
    sys.stderr.write(f"[loopback] serving {len(frames)} frames on :{port}\n")
    sys.stderr.flush()
    seen = serve_once(frames, port, ping_after=ping_after)
    sys.stderr.write(f"[loopback] client sent {len(seen)} frames; "
                     f"pongs={pongs(seen)}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
