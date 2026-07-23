#!/usr/bin/env python3
"""Capture Anvil live WebSocket traffic to an NDJSON replay trace.

DepthCharge M0 ground-truth capture. Connects to the Anvil demo WebSocket
(``wss://anvil.garethcooke.com/ws?ticker=<id>``) and records every received
frame verbatim, one JSON object per line, for deterministic replay in the host
harness.

Output format (matches the M0 brief):

  line 1  : metadata object  {captured_at, url, ticker, tool_version, ...}
  line 2+ : one per received frame  {"rx_ns": <monotonic-ns>, "frame": <verbatim JSON>}

Frames are stored **verbatim**: the exact JSON text the server sent is spliced
into the ``frame`` field with no re-serialisation, so key order and number
formatting are byte-preserved. Each line is still a single valid JSON object
because every Anvil WS frame is itself one complete JSON object.

Deliberately dependency-free: a small RFC 6455 client over ``ssl`` + ``socket``
from the standard library only. The M0 brief suggested the ``websockets``
package, but the owner's box has no ``pip``/``ensurepip``, and a stdlib-only
tool is strictly more portable ("no exotic dependencies"). The trade-off is a
hand-rolled framer; it only has to read server->client text/close/ping frames
and answer pings, which is a small, well-tested surface. See the M0 session log.

Reconnect trace: ``--cycles N --reconnect-after S`` opens the socket N times,
capturing S seconds each, so the trace contains N ``snapshot`` frames and the
per-connection ``seq`` baseline reset is on record without touching the host
network.

Python 3 stdlib only. Lives in tools/ (Python is allowed nowhere else).
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import signal
import socket
import ssl
import struct
import sys
import time
from datetime import datetime, timezone
from urllib.parse import urlsplit

TOOL_VERSION = "0.1.0"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  # RFC 6455 magic

# WebSocket opcodes
OP_CONT = 0x0
OP_TEXT = 0x1
OP_BINARY = 0x2
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA


class WsError(RuntimeError):
    """WebSocket protocol / handshake failure."""


class HandshakeRejected(WsError):
    """The upgrade was refused (non-101 status). Carries the status line."""

    def __init__(self, status_line: str, headers: dict[str, str]):
        super().__init__(f"upgrade rejected: {status_line}")
        self.status_line = status_line
        self.headers = headers


class WsClient:
    """Minimal RFC 6455 text client: connect, iterate messages, close.

    Reads unmasked server frames, reassembles continuation fragments, answers
    ping with pong, and masks the few frames it sends (close). It never sends
    application data — this is a pure market-data consumer.
    """

    def __init__(self, url: str, origin: str | None, timeout: float = 20.0):
        parts = urlsplit(url)
        if parts.scheme not in ("ws", "wss"):
            raise WsError(f"unsupported scheme {parts.scheme!r} (want ws/wss)")
        self.tls = parts.scheme == "wss"
        self.host = parts.hostname
        self.port = parts.port or (443 if self.tls else 80)
        self.path = parts.path or "/"
        if parts.query:
            self.path += "?" + parts.query
        self.origin = origin
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self._buf = bytearray()
        # Recorded from the handshake so the caller can answer the Origin
        # known-unknown empirically.
        self.handshake_status = ""

    # -- connection lifecycle -------------------------------------------------

    def connect(self) -> None:
        raw = socket.create_connection((self.host, self.port), timeout=self.timeout)
        if self.tls:
            ctx = ssl.create_default_context()
            self.sock = ctx.wrap_socket(raw, server_hostname=self.host)
        else:
            self.sock = raw
        self.sock.settimeout(1.0)  # poll cadence so recv can observe deadlines
        self._handshake()

    def _handshake(self) -> None:
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        lines = [
            f"GET {self.path} HTTP/1.1",
            f"Host: {self.host}",
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {key}",
            "Sec-WebSocket-Version: 13",
            "User-Agent: depthcharge-capture/" + TOOL_VERSION,
        ]
        if self.origin is not None:
            lines.append(f"Origin: {self.origin}")
        request = ("\r\n".join(lines) + "\r\n\r\n").encode("ascii")
        self.sock.sendall(request)

        header_bytes = self._read_until(b"\r\n\r\n")
        head = header_bytes.decode("iso-8859-1")
        status_line, _, rest = head.partition("\r\n")
        self.handshake_status = status_line.strip()
        headers: dict[str, str] = {}
        for line in rest.split("\r\n"):
            if ":" in line:
                name, _, val = line.partition(":")
                headers[name.strip().lower()] = val.strip()

        parts = status_line.split(" ", 2)
        code = parts[1] if len(parts) > 1 else ""
        if code != "101":
            raise HandshakeRejected(status_line.strip(), headers)

        accept = headers.get("sec-websocket-accept", "")
        expect = base64.b64encode(
            hashlib.sha1((key + WS_GUID).encode("ascii")).digest()
        ).decode("ascii")
        if accept != expect:
            raise WsError("Sec-WebSocket-Accept mismatch (bad handshake)")

    def _read_until(self, needle: bytes) -> bytes:
        """Read into the buffer until *needle* appears; return through it."""
        deadline = time.monotonic() + self.timeout
        while True:
            idx = self._buf.find(needle)
            if idx != -1:
                end = idx + len(needle)
                out = bytes(self._buf[:end])
                del self._buf[:end]
                return out
            if time.monotonic() > deadline:
                raise WsError("timed out reading handshake response")
            self._fill()

    def _fill(self) -> int:
        """Pull one chunk into the buffer. Returns bytes read (0 on timeout)."""
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            return 0
        if chunk == b"":
            raise ConnectionError("server closed the connection")
        self._buf.extend(chunk)
        return len(chunk)

    # -- frame parsing --------------------------------------------------------

    def _next_frame(self, deadline: float) -> tuple[int, bytes] | None:
        """Return (opcode, payload) for the next frame, or None on deadline."""
        while True:
            frame = self._try_parse_frame()
            if frame is not None:
                return frame
            if _STOP or time.monotonic() > deadline:
                return None  # honour Ctrl-C even while the stream is quiet
            self._fill()

    def _try_parse_frame(self) -> tuple[int, bytes] | None:
        b = self._buf
        if len(b) < 2:
            return None
        b0, b1 = b[0], b[1]
        fin = b0 & 0x80
        opcode = b0 & 0x0F
        masked = b1 & 0x80
        length = b1 & 0x7F
        offset = 2
        if length == 126:
            if len(b) < offset + 2:
                return None
            length = struct.unpack_from(">H", b, offset)[0]
            offset += 2
        elif length == 127:
            if len(b) < offset + 8:
                return None
            length = struct.unpack_from(">Q", b, offset)[0]
            offset += 8
        mask_key = b""
        if masked:
            if len(b) < offset + 4:
                return None
            mask_key = bytes(b[offset:offset + 4])
            offset += 4
        if len(b) < offset + length:
            return None
        payload = bytes(b[offset:offset + length])
        if masked:  # servers must not mask, but tolerate defensively
            payload = bytes(p ^ mask_key[i % 4] for i, p in enumerate(payload))
        del b[:offset + length]
        return (fin | opcode, payload)

    def messages(self, deadline: float):
        """Yield (rx_ns, text) for each complete text message until deadline.

        Handles fragmentation (continuation frames) and answers control frames.
        rx_ns is captured the instant the final fragment of the message arrives.
        """
        assembling: list[bytes] = []
        assembling_op = None
        while True:
            frame = self._next_frame(deadline)
            if frame is None:
                return  # deadline reached
            header, payload = frame
            fin = header & 0x80
            opcode = header & 0x0F

            if opcode == OP_PING:
                self._send_frame(OP_PONG, payload)
                continue
            if opcode == OP_PONG:
                continue
            if opcode == OP_CLOSE:
                try:
                    self._send_frame(OP_CLOSE, payload[:2])
                except OSError:
                    pass
                raise ConnectionError("server sent CLOSE")

            if opcode == OP_CONT:
                if assembling_op is None:
                    raise WsError("continuation frame with nothing to continue")
                assembling.append(payload)
            elif opcode in (OP_TEXT, OP_BINARY):
                assembling = [payload]
                assembling_op = opcode
            else:
                raise WsError(f"unexpected opcode {opcode:#x}")

            if fin:
                rx_ns = time.monotonic_ns()
                data = b"".join(assembling)
                op = assembling_op
                assembling, assembling_op = [], None
                if op == OP_TEXT:
                    yield rx_ns, data.decode("utf-8")
                # binary frames are unexpected on this stream; skip silently

    def _send_frame(self, opcode: int, payload: bytes = b"") -> None:
        mask = os.urandom(4)
        masked = bytes(p ^ mask[i % 4] for i, p in enumerate(payload))
        header = bytearray([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        header += mask
        self.sock.sendall(bytes(header) + masked)

    def close(self) -> None:
        if self.sock is None:
            return
        try:
            self._send_frame(OP_CLOSE, struct.pack(">H", 1000))
        except OSError:
            pass
        try:
            self.sock.close()
        finally:
            self.sock = None


# -- capture orchestration ----------------------------------------------------

_STOP = False


def _install_sigint():
    def handler(signum, frame):  # noqa: ARG001
        global _STOP
        _STOP = True
    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)


def _connect_with_origin_probe(url: str, origin: str | None, timeout: float):
    """Connect, transparently retrying with a nominated Origin if the server
    rejects an Origin-less upgrade. Returns (client, effective_origin)."""
    client = WsClient(url, origin=origin, timeout=timeout)
    try:
        client.connect()
        return client, origin
    except HandshakeRejected as exc:
        client.close()  # tear the first socket down cleanly before retrying
        if origin is not None:
            raise
        host = urlsplit(url).hostname
        fallback = f"https://{host}"
        sys.stderr.write(
            f"[capture] Origin-less upgrade rejected ({exc.status_line}); "
            f"retrying with Origin: {fallback}\n"
        )
        client = WsClient(url, origin=fallback, timeout=timeout)
        client.connect()
        return client, fallback


def capture(args) -> int:
    _install_sigint()
    captured_at = datetime.now(timezone.utc).isoformat()
    url = f"{args.url}?ticker={args.ticker}"

    frame_count = 0
    kind_counts: dict[str, int] = {}
    reconnects = 0
    effective_origin = args.origin
    origin_note = None

    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        # Metadata header line (line 1). Written first, patched-in fields last.
        # We stream frames as we go, so metadata is written up front with the
        # fields known at connect time; per-run summary is printed to stderr.
        meta_placeholder_pos = None
        cycles = max(1, args.cycles)
        per_cycle = args.duration if cycles == 1 else (args.reconnect_after or args.duration)

        # Establish the first connection to resolve the Origin question before
        # writing metadata (so the header records what actually worked).
        client, effective_origin = _connect_with_origin_probe(
            url, args.origin, timeout=args.connect_timeout
        )
        if args.origin is None and effective_origin is not None:
            origin_note = "server required a nominated Origin header"
        elif args.origin is None:
            origin_note = "server accepted upgrade with no Origin header"

        meta = {
            "captured_at": captured_at,
            "url": url,
            "ticker": args.ticker,
            "tool_version": TOOL_VERSION,
            "capture_mode": "reconnect" if cycles > 1 else "baseline",
            "cycles": cycles,
            "origin_sent": effective_origin,
            "origin_note": origin_note,
            "handshake_status": client.handshake_status,
        }
        out.write(json.dumps(meta) + "\n")
        out.flush()

        try:
            for cycle in range(cycles):
                if cycle > 0:
                    if _STOP:
                        break
                    if args.reconnect_gap > 0:
                        sys.stderr.write(
                            f"[capture] simulated drop: {args.reconnect_gap}s gap\n"
                        )
                        gap_end = time.monotonic() + args.reconnect_gap
                        while time.monotonic() < gap_end and not _STOP:
                            time.sleep(0.25)  # stay responsive to Ctrl-C
                        if _STOP:
                            break
                    client, effective_origin = _connect_with_origin_probe(
                        url, effective_origin, timeout=args.connect_timeout
                    )
                    reconnects += 1
                    sys.stderr.write(f"[capture] reconnect #{reconnects}\n")

                deadline = time.monotonic() + per_cycle
                try:
                    for rx_ns, text in client.messages(deadline):
                        if _STOP:
                            break
                        # Verbatim: validate it parses, but write original text.
                        try:
                            obj = json.loads(text)
                        except json.JSONDecodeError:
                            sys.stderr.write("[capture] skipping non-JSON frame\n")
                            continue
                        out.write('{"rx_ns": %d, "frame": %s}\n' % (rx_ns, text))
                        frame_count += 1
                        kind = obj.get("type", "?") if isinstance(obj, dict) else "?"
                        kind_counts[kind] = kind_counts.get(kind, 0) + 1
                        if frame_count % 200 == 0:
                            out.flush()
                            sys.stderr.write(
                                f"[capture] {frame_count} frames {dict(kind_counts)}\r"
                            )
                        if args.max_frames and frame_count >= args.max_frames:
                            raise KeyboardInterrupt
                except (ConnectionError, ssl.SSLError) as exc:
                    sys.stderr.write(f"\n[capture] connection ended: {exc}\n")
                finally:
                    client.close()
                if _STOP or (args.max_frames and frame_count >= args.max_frames):
                    break
        except KeyboardInterrupt:
            pass
        finally:
            out.flush()

    sys.stderr.write("\n")
    sys.stderr.write(
        f"[capture] done: {frame_count} frames, kinds={kind_counts}, "
        f"reconnects={reconnects}, origin_sent={effective_origin!r}, "
        f"handshake={client.handshake_status!r}\n"
    )
    sys.stderr.write(f"[capture] wrote {args.out}\n")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--url", default="wss://anvil.garethcooke.com/ws",
                   help="WS base URL (ticker query is appended)")
    p.add_argument("--ticker", type=int, default=101, help="ticker id to subscribe")
    p.add_argument("--out", required=True, help="output NDJSON path")
    p.add_argument("--duration", type=float, default=300.0,
                   help="capture seconds (single-connection baseline)")
    p.add_argument("--cycles", type=int, default=1,
                   help="number of connect cycles (>1 => reconnect trace)")
    p.add_argument("--reconnect-after", type=float, default=0.0,
                   help="seconds per cycle before reconnecting (reconnect mode)")
    p.add_argument("--reconnect-gap", type=float, default=0.0,
                   help="seconds to stay disconnected between cycles, simulating "
                        "a network drop so the gap is visible in rx_ns")
    p.add_argument("--origin", default=None,
                   help="Origin header to send (default: none, to probe the "
                        "server's allowlist; auto-retries with https://<host> "
                        "if an Origin-less upgrade is rejected)")
    p.add_argument("--max-frames", type=int, default=0, help="stop after N frames")
    p.add_argument("--connect-timeout", type=float, default=20.0)
    args = p.parse_args(argv)
    try:
        return capture(args)
    except HandshakeRejected as exc:
        sys.stderr.write(f"[capture] handshake rejected: {exc.status_line}\n")
        return 2
    except OSError as exc:
        sys.stderr.write(f"[capture] network error: {exc}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
