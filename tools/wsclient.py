#!/usr/bin/env python3
"""The stdlib RFC 6455 client the capture tools share.

Extracted verbatim from ``capture_anvil.py`` at M4 stage 0, when a second venue
(Kraken) needed the same client. The M4 stage-0 brief's rule was: extract it
**only if ``capture_anvil.py``'s output stays byte-identical across the
refactor**, otherwise clone and say so. It does, and the check was executed
rather than argued -- see "How the extraction was verified" below.

Deliberately dependency-free: ``ssl`` + ``socket`` only. The owner's box has no
``pip``/``ensurepip``, so a stdlib-only tool is strictly more portable. The
trade-off is a hand-rolled framer; it reads server->client text/close/ping
frames, answers pings, and masks the few frames it sends.

What changed in the move, and why each change cannot move a byte of Anvil's
trace:

  * ``_STOP`` and ``_install_sigint`` moved here, because the client's read loop
    is what has to observe Ctrl-C while the stream is quiet. Both tools now
    share one flag. Same semantics, same two signals.
  * ``user_agent`` is a constructor argument instead of a module constant, and
    both tools pass ``depthcharge-capture/<their TOOL_VERSION>``.
    ``capture_anvil.py``'s TOOL_VERSION is still 0.1.0, so the handshake bytes
    it sends are unchanged.
  * ``clock`` is a constructor argument, defaulted to ``time.monotonic_ns`` --
    the one ``capture_anvil.py`` has always used -- so the field's meaning in an
    Anvil trace is unchanged. ``capture_kraken.py`` passes
    ``time.perf_counter_ns`` and says why: measured on this box,
    ``time.monotonic_ns()`` advances in **15.0 ms** steps, which is coarser than
    the gaps a Kraken capture exists to measure. (The M3 addendum in
    ``harness/replay/NOTES.md`` proposed raising the process timer resolution as
    the cure for exactly this; measured 2026-08-16, ``timeBeginPeriod(1)`` does
    **not** move ``monotonic_ns`` on Python 3.12 / Windows 11 -- still 15.0 ms.
    A different clock does: ``perf_counter_ns`` steps at 0.2 us.)
  * ``send_text()`` is NEW and is the one genuinely new capability at M4 stage 0
    -- ``capture_anvil.py`` is a pure consumer, while Kraken has to *send* a
    subscribe. It reuses the existing masking path that ``close()`` already
    needed, so nothing on the Anvil path is touched by its existence.

**How the extraction was verified** (M4 stage 0, 2026-08-16). A capture is a
live-network sample, so two runs against Kraken or Anvil can never be compared
byte for byte -- the frames themselves differ. The check therefore replays a
*committed* trace at the client instead: a throwaway loopback WS server
(stdlib, scratchpad only) serves the 1,513 frames of
``harness/replay/anvil_101_baseline_20260809.ndjson`` over ``ws://127.0.0.1``,
and the pre-refactor and post-refactor ``capture_anvil.py`` are each run against
it. With the two timestamp fields normalised (``rx_ns``, which is a clock, and
``captured_at``, which is a date) the two outputs are **byte-identical over all
1,514 lines**, including the metadata header and every frame's key order and
number formatting. That is the property the brief asked for, and it is the only
one a live capture cannot demonstrate.

Python 3 stdlib only. Lives in tools/ (Python is allowed nowhere else).
"""
from __future__ import annotations

import base64
import hashlib
import itertools
import os
import signal
import socket
import ssl
import struct
import time
from urllib.parse import urlsplit

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


# -- interruption -------------------------------------------------------------
# The flag lives beside the read loop that has to honour it: `_next_frame` must
# be able to return on Ctrl-C even when the venue has gone quiet, which is the
# whole reason this is a module global rather than a parameter.

_STOP = False


def install_sigint() -> None:
    """Make Ctrl-C / SIGTERM set the stop flag instead of raising."""

    def handler(signum, frame):  # noqa: ARG001
        global _STOP
        _STOP = True
    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)


def stop_requested() -> bool:
    return _STOP


class WsClient:
    """Minimal RFC 6455 text client: connect, iterate messages, close.

    Reads unmasked server frames, reassembles continuation fragments, answers
    ping with pong, and masks the frames it sends (close, and from M4 stage 0
    an application text frame via `send_text`).
    """

    def __init__(self, url: str, origin: str | None, timeout: float = 20.0,
                 user_agent: str = "depthcharge-capture",
                 clock=time.monotonic_ns):
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
        self.user_agent = user_agent
        # Which monotonic clock stamps rx_ns. Injectable, and defaulted to the
        # one capture_anvil.py has always used, so the extraction cannot change
        # the meaning of a field in the Anvil traces. capture_kraken.py passes
        # time.perf_counter_ns and says why in its header: on this box
        # time.monotonic_ns() advances in 15.0 ms steps, which is coarser than
        # Kraken's own inter-message gaps.
        self.clock = clock
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
            # server_hostname is what puts SNI on the wire. Anvil does not need
            # it; Kraken is fronted by Cloudflare and answers a missing SNI with
            # a 403 rather than a handshake error, so it is load-bearing there.
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
            "User-Agent: " + self.user_agent,
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
        """Pull one chunk into the buffer. Returns bytes read (0 on timeout).

        TWO ways a poll can come back empty, and only one of them was handled
        until M5 stage 0 (2026-08-24), when Binance found the other.

        `socket.timeout` is the plain case: the deadline passed with nothing
        readable. `ssl.SSLWantReadError` is the case a TLS socket adds -- the
        deadline passed with a **partially received TLS record** in hand, so the
        SSL layer cannot return a plaintext chunk yet and needs more bytes. It is
        NOT an error and it is NOT a closed connection: the partial record is held
        inside the SSL object and the next `recv` resumes it. Returning 0 is the
        correct answer to both.

        Why no venue found this before. It fires only when a frame straddles the
        1.0 s poll boundary, which needs the socket to be quiet for most of a
        second and then to speak. Anvil summarises every 500 ms and Kraken
        heartbeats at 1 Hz, so on both venues a poll almost always completes
        inside one record. Binance's `@depth`/`@depth20` at the **1000 ms**
        cadence sits exactly on the boundary, and the first capture 2 attempt
        died after 4 frames with

            connection ended: The operation did not complete (read) (_ssl.c:2624)

        which `capture_*.py` reports as `ssl.SSLError` and treats as the end of
        the stream -- an ordinary quiet second, misread as a dead socket. On a
        quiet pair (capture 3) it would have ended the capture almost at once.
        This is the shape ARCHITECTURE §9's never-observed row describes: a code
        path whose correctness had never been exercised because no committed
        trace had ever contained the condition.
        """
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            return 0
        except ssl.SSLWantReadError:
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
            payload = bytes(b ^ m for b, m in zip(payload, itertools.cycle(mask_key)))
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
                rx_ns = self.clock()
                data = b"".join(assembling)
                op = assembling_op
                assembling, assembling_op = [], None
                if op == OP_TEXT:
                    yield rx_ns, data.decode("utf-8")
                # binary frames are unexpected on this stream; skip silently

    def send_text(self, text: str) -> int:
        """Send one application text frame; return the monotonic ns it left.

        NEW at M4 stage 0. Anvil captures never needed it — that stream is a
        pure consumer — but a Kraken book subscription is a client->server
        frame, so the capture tool has to be able to talk. The timestamp is
        returned rather than stamped internally because the caller writes it
        into the trace, and a sent frame belongs in the record exactly as much
        as a received one: it is what provoked everything after it.
        """
        self._send_frame(OP_TEXT, text.encode("utf-8"))
        return self.clock()

    def _send_frame(self, opcode: int, payload: bytes = b"") -> None:
        mask = os.urandom(4)
        masked = bytes(b ^ m for b, m in zip(payload, itertools.cycle(mask)))
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
