// firmware/src/ws_transport.hpp — Wi-Fi + TLS WebSocket, and nothing else.
//
// Everything ESP-IDF-shaped lives on this side of the line: Wi-Fi, esp-tls, the
// socket, and the driving of the frame parser and the reassembler. What crosses
// into the rest of the firmware is a FramePipe slot of verbatim wire bytes —
// which is exactly what the harness's TraceReader hands the adapter, and why the
// engine cannot tell the difference (invariant #1).
//
// THE WEBSOCKET CLIENT IS OURS. `esp_websocket_client` was removed on
// 2026-08-16 after the bench convicted it (ARCHITECTURE §9, 2026-08-13: 17/17
// errno-silent deaths, 85 co-timed `SPLIT@1` framing rejects) and the
// replacement's soak acquitted this one (23.6 h, connects=1 across a 10.9 h
// segment, zero deaths, zero rejects). With it went the two-handle design, the
// 5 s sleeper it was built to dodge, the esp_event dispatch hop and the
// DC_OWNED_WS switch that kept both arms buildable while the question was open.
//
// The frame *layer* and the reassembly *logic* are deliberately not here: they
// are in ws_frame.hpp and frame_reassembler.hpp, free of ESP-IDF so the host
// suite can test them. This class is the platform half — a socket, a task, and
// the autopsy — and it owns no engine state and never touches the book.
#pragma once

// Arduino.h FIRST, and not by accident. The lwIP headers this file and its .cpp
// reach (esp_tls.h, and lwip/sockets.h + lwip/netdb.h over there) define
// INADDR_NONE as a macro; Arduino's IPAddress.h then declares
// `extern IPAddress INADDR_NONE;` and the macro rewrites the declaration into a
// syntax error. Including Arduino.h first makes IPAddress.h win, which is the
// order the framework expects. Doing it here rather than in the .cpp means no
// future includer of this header can get it wrong.
#include <Arduino.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_tls.h"
#include "anvil_endpoint.hpp"
#include "ws_frame.hpp"

#include "frame_pipe.hpp"
#include "frame_reassembler.hpp"
#include "rx_budget.hpp"
#include "stall_probe.hpp"
#include "ws_ping.hpp"
#include "ws_supervisor.hpp"

namespace depthcharge::fw {

// The endpoint — host, path, port — is nvil_endpoint.hpp, shared with the
// diag client so the two cannot drift. See that file for why it moved.

// The client's Sec-WebSocket-Key, and the accept value it obliges the server to
// return: base64(SHA1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")), RFC 6455
// §4.1. Both are constants because the key is, which is the whole trick — the
// verification costs one string compare and brings no SHA1 dependency onto the
// board. `tools/` can regenerate the pair in three lines of Python if the key
// ever changes.
//
// A fixed key is a deliberate, bounded departure from §4.1's "randomly
// selected". What the randomness defends against is a cached or canned response
// from something that is not a WebSocket server; this client speaks TLS to one
// pinned host with a pinned root CA, so there is no intermediary to be fooled
// and nothing to cache the exchange. What we get in return is a check the
// prototype skipped entirely: if the upgrade is ever answered by something that
// did not do the SHA1, the connect fails loudly here instead of the frame parser
// spending a socket discovering that HTML is not a WebSocket frame.
inline constexpr char kWsKey[] = "ZGVwdGhjaGFyZ2Utd3MwMQ==";
inline constexpr char kWsAccept[] = "D0+kBNqSZmwk6149E3G25IPQoN4=";

// SO_RCVTIMEO on the socket. A read that times out is NOT a death — Anvil's
// worst healthy inter-frame gap is 391-594 ms and a weak association adds
// measured fades to 3.9 s — it is the RX task's chance to notice a client ping
// is due and to see a reconnect request. Silence is the feed task's business:
// kRxWatchdogMs (1000 ms) greys the panel and stays exactly where it is.
inline constexpr std::uint32_t kReadTimeoutMs = 1000;

// Client->server pings: ON, and the question they answer is not the one the
// 60 s arm was built for.
//
// THE OLD QUESTION IS STILL CLOSED. A ping was originally proposed to PROVOKE a
// half-open TCP connection into an error the socket would report, and that is
// answered from the other side: `WsSupervisor` acts on the silence directly
// (kSilenceRecycleUs, five minutes, host-tested), which needs no server
// cooperation and no write into a fade. Nothing below re-opens it, and the
// recycle remains the only recovery path.
//
// THE NEW QUESTION IS WHOSE FAULT THE STALENESS IS. `staleness.hpp` says how old
// the book is; it cannot say whether the age is this socket's server-side send
// queue (ROADMAP A7 is then the fix) or lag upstream of it in the broadcaster
// (A7 would not help). A round-trip separates them, because Anvil's vendored
// Crow queues a pong behind everything already posted to this connection. The
// argument, the bound and what may NOT be concluded from a fast pong are all in
// `ws_ping.hpp`; the policy is host-tested in `harness/tests/test_ws_ping.cpp`
// and the cadence constant lives there with it.
//
// The switch survives as a control arm rather than as a default, inverted from
// what it used to be: `depthcharge-noping` builds the pingless firmware, so a
// bench session can still attribute a change to this and nothing else. It is
// `DC_WS_PING` / `kClientPingEnabled`, and it is declared in `ws_ping.hpp`
// beside the thing it switches — `PingProbe::render` has to know about it too,
// so that the pingless arm says "disabled at build" rather than printing "no
// round-trip yet" forever and reading exactly like a venue that stopped
// answering. Two spellings of one build flag is how those two ends drift.

// The RX task. Core 0 by the brief, which is the thing the old client could
// never do: esp_websocket_client_config_t in this vintage has no task_core_id,
// so its task landed wherever the scheduler put it and shared cores with the
// panel's DMA-fed render task by luck. Core 0 is the feed core, so a message now
// arrives, is reassembled and is parsed without ever crossing a core boundary.
//
// Priority 5 matches the feed task deliberately — a socket read that preempted
// the book would trade a grey panel for a late one — and the stack is sized for
// mbedtls's handshake, which is the deepest thing this task ever does. Both are
// printed as a high-water mark on every death so the next bench session can
// lower them on evidence instead of on nerve.
inline constexpr std::uint32_t kRxTaskStack = 6144;
inline constexpr UBaseType_t kRxTaskPriority = 5;
inline constexpr BaseType_t kRxTaskCore = 0;

// Wi-Fi modem sleep: OFF here, ON in the `depthcharge-ps` build environment.
//
// This is a knob rather than a constant because it is the first experiment run
// through the arrival-vs-event instrument (strain 12), and an experiment needs
// both arms buildable without editing a line. `pio run -e depthcharge` is the
// baseline; `pio run -e depthcharge-ps` is the same firmware with modem sleep
// left at the Arduino default.
//
// The default is 0 — power save OFF — and has been since the stage C draft,
// which matters for how the result must be read: the M3 characterisation brief
// expected power save to be default-on under Arduino, and it IS (Arduino-ESP32
// 2.0.14 initialises `_sleepEnabled` to `WIFI_PS_MIN_MODEM` on every target but
// the S2), but this firmware has always turned it off in connect_wifi(). So the
// experiment is not "does turning it off help" — it is "was turning it off ever
// taking effect, and does turning it back ON reproduce the stall", which is the
// same fork answered from the other side and needs no assumption about which
// call wins. The readback in connect_wifi() removes the assumption entirely.
#ifndef DC_WIFI_POWER_SAVE
#define DC_WIFI_POWER_SAVE 0
#endif
inline constexpr bool kWifiPowerSave = (DC_WIFI_POWER_SAVE != 0);

// The RX buffer, deliberately smaller than an Anvil book frame.
//
// It could be set past 8,726 bytes so that most messages arrive in a single
// event, but that would leave the reassembly path — the code that has to be
// right when a message *is* split — running only rarely, and rarely-run code on
// a device that must not stall is code that rots. At 4 KiB every 8.7 KB book
// frame arrives in three chunks, so reassembly is exercised ~12 times a second
// and any bug in it shows up on the first bench run rather than in a month.
inline constexpr int kWsRxBufferBytes = 4096;

// The reconnect constants and the policy that uses them live in
// ws_supervisor.hpp, which is ESP-IDF-free and therefore host-tested. What used
// to be here — a 2 s backoff, and the claim that it preempted the library's
// unreachable 10 s `WEBSOCKET_RECONNECT_TIMEOUT_MS` — was true about the config
// struct and wrong about the effect: the library's task sleeps 5 s inside every
// abort, `esp_websocket_client_stop()` blocks for whatever is left of it, and
// the two are anti-correlated to a constant. That measurement, and the change it
// forced (a spare client handle, so nothing waits for the sleeper), are argued
// in ws_supervisor.hpp's header comment.

// How often the supervisor samples rssi.
//
// It lives here rather than in the console because this class is the only one
// that already owns Wi-Fi driver calls, and because the console must not include
// <Arduino.h>: doing so would let esp32-hal-log.h redefine its ESP_LOGx onto
// Arduino's log_printfv, which mallocs past 64 characters and takes the UART
// mutex — the path feed_task.cpp had all its logging removed for. So the sample
// is taken on loopTask, inside supervise(), which is already there every 250 ms.
//
// 500 ms gives ~1,200 samples over a ten-minute run and bounds how stale the
// figure attached to a hole can be at half a second, which is well inside the
// timescale over which 2.4 GHz conditions change.
inline constexpr std::int64_t kRssiPeriodUs = 500 * 1000;

class WsTransport {
public:
    // `link` is written here and read by the feed task and console — one writer,
    // 8- and 32-bit fields, the same unsynchronised-diagnostics trade as every
    // counter in this firmware.
    WsTransport(FramePipe& pipe, LinkQuality& link) noexcept
        : pipe_(pipe), link_(link), reassembler_(pipe, kFrameCapacity), parser_(*this) {}

    // Blocks until the station has an IP or `timeout_ms` elapses.
    bool connect_wifi(const char* ssid, const char* password,
                      std::uint32_t timeout_ms = 30000) noexcept;

    // Creates the RX task on Core 0 and asks it for the first connection.
    // Recovery here is transport-driven, never seq-driven — on reconnect Anvil
    // sends a fresh snapshot and the phase-1 book adopts it (protocol §4).
    //
    // ONE CONNECTION, NOT TWO, and that is the largest thing this rebuild
    // deletes. The spare-handle design (2026-08-10, ARCHITECTURE §9) existed for
    // exactly one reason: `esp_websocket_client`'s task slept 5 s inside every
    // abort and `esp_websocket_client_stop()` blocked for whatever was left of
    // it, so the only way to get a live socket back inside 5 s was to already
    // own a second one. `esp_tls_conn_destroy()` is ours and returns at once, so
    // the reason is gone and the second context goes with it: ~10 KiB of heap
    // for the spare's buffers, its 6 KiB task stack, `disable_auto_reconnect`,
    // the live_/spare bookkeeping and the guard that stopped a retired handle's
    // DATA reaching the reassembler. `kClientSelfExitUs` and the two-handle
    // assert went with the arm on 2026-08-16.
    bool start() noexcept;

    // Polls the socket's state and runs the two supervisors. Same shape and the
    // same policy objects as before — WsSupervisor and WifiSupervisor are
    // untouched by this rebuild, which is deliberate: they are the host-tested
    // part, and a client swap that also moved the reconnect arithmetic would
    // have nothing left to attribute a change in behaviour to.
    //
    // What is different is what StartAttempt costs. It used to call
    // `esp_websocket_client_start()` here on loopTask, behind a blocking DNS
    // warm; it now sets a flag the RX task picks up, and the RX task does the
    // resolving and the ~4 s blocking handshake on Core 0. loopTask no longer
    // blocks on the network at all.
    void supervise() noexcept;

    bool connected() const noexcept { return socket_up_.load(std::memory_order_relaxed); }

    // --- WsFrameParser's handler ---------------------------------------------
    // Public because WsFrameParser is a template over this type, not because
    // anything else may call them: every one runs on the RX task, inside
    // parser_.feed(), on bytes that task just read.
    // THE SILENCE RECYCLE'S CLOCK IS STAMPED HERE, not at the read, and moving
    // it was forced by turning the client ping on. See `last_data_us_`.
    void on_chunk(const WsChunk& c) noexcept {
        last_data_us_.store(c.arrival_us, std::memory_order_relaxed);
        reassembler_.on_chunk(c);
    }
    void on_ping(const std::uint8_t* payload, std::uint32_t len) noexcept;
    void on_pong() noexcept;
    void on_close(std::uint16_t code, const char* reason, std::uint32_t reason_len) noexcept;
    // Deliberately empty. The parser latches the error and stops reading, and
    // rx_main() acts on it after feed() returns — because the socket cannot be
    // destroyed from inside a callback that is walking a buffer owned by the
    // read that is still on the stack.
    void on_protocol_error(WsProtocolError) noexcept {}

private:
    static void rx_trampoline(void* arg) noexcept;

    // The whole socket lifecycle, on one task: connect when asked, read until it
    // dies, autopsy it, wait to be asked again.
    //
    // It is one task and not two halves because every alternative splits an
    // operation that must not be split. The connect blocks for ~4 s (DNS, TCP,
    // TLS against the pinned root, the upgrade); doing it on loopTask would stop
    // the 250 ms supervise poll for the duration, and doing it on the feed task
    // would stop the book. The read blocks for up to kReadTimeoutMs; the same
    // argument applies. And a socket owned by one task needs no synchronisation
    // at all around `tls_` — which is the difference between this and the two
    // handles, an atomic and a comment about which of them may deliver data.
    [[noreturn]] void rx_main() noexcept;

    // DNS, TCP, TLS and the upgrade. False means it did not come up, with the
    // autopsy already printed.
    bool open_socket() noexcept;
    void close_socket() noexcept;

    // The 101, hand-rolled, with Sec-WebSocket-Accept verified against the
    // constant in kWsAccept. Headers are consumed byte-accurately so that
    // anything the server sent after the blank line is still in the socket for
    // the frame parser — reading ahead into a buffer we then discarded is one of
    // the two ways to manufacture the very corruption this rebuild removes.
    bool http_upgrade() noexcept;

    // THE INSTRUMENT THE OLD CLIENT MADE IMPOSSIBLE. One line per socket end,
    // carrying the mbedtls rc, errno, SO_ERROR, the lifetime, the bytes, the
    // frame counts, the close code and reason if the server sent one, the rssi,
    // and the association's state at the moment of death.
    //
    // Read it the way link_autopsy.cpp's header says to: rc -0x0050 / errno 104
    // is an active RST, -0x7180 / -0x7200 is corruption TLS caught, -0x7780 is
    // the server objecting out loud, rc 0 is a clean close, and ~constant bytes
    // across deaths means a byte-counting middlebox where ~constant seconds
    // means a timer. `errno=119` with nothing else — the stale EINPROGRESS of
    // the 2026-08-13 capture, 17 times out of 17 — is what this client is
    // supposed to have made impossible, and if it appears here it means the
    // fault was never the library's.
    //
    // It logs from the RX task, on a socket that is already dead, which is the
    // same rule the old on_event() carried: ESP_LOGx is Arduino's log_x here
    // (Arduino.h comes first for the INADDR_NONE fix), so it mallocs past 64
    // characters and takes the UART mutex — affordable only where there is no
    // frame left to delay.
    void autopsy(const char* what, int rc, int saved_errno) noexcept;

    // autopsy() then close_socket(), in that order, structurally — the autopsy
    // reads state the teardown destroys. Every death path goes through here.
    void die(const char* what, int rc, int saved_errno) noexcept;

public:
    // The RX loop's stopwatch (rx_budget.hpp): written only by the RX task,
    // diffed by the statistics block. Const access only — the budget is an
    // instrument, and instruments are read, never steered.
    const RxBudget& rx_budget() const noexcept { return budget_; }

    // The client-ping round-trip (ws_ping.hpp): written only by the RX task,
    // rendered by the statistics block. Const for the same reason, and with one
    // more of its own — §6 #5. Nothing may branch on this to decide the panel is
    // live: a server can answer pongs perfectly while publishing nothing, so a
    // caller that could turn the panel green on control traffic would be drawing
    // a frozen ladder that reads LIVE. Const access makes "report only" the
    // shape of the type rather than a rule someone has to keep.
    const PingProbe& ping_probe() const noexcept { return probe_; }

private:
    RxBudget budget_;
    PingProbe probe_;

    // Writes `len` bytes or reports failure; esp_tls_conn_write may take fewer.
    bool write_all(const void* data, std::size_t len) noexcept;

    // Sends a client ping if kClientPingMs says one is due. False means the
    // write failed, the socket has been closed, and the caller must not read it.
    bool maybe_ping(std::int64_t now) noexcept;

    // Scans for `ssid`, prints every sibling that answers with its channel and
    // signal, and hands back the strongest. False means the scan found none, in
    // which case the caller falls back to letting the driver choose.
    //
    // WHY THE DRIVER'S OWN CHOICE IS NOT TRUSTED, on evidence rather than
    // suspicion. `WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL` were
    // added on 2026-08-13 to replace the framework's FAST_SCAN lottery, and they
    // FAILED their five-boot acceptance the same evening: five resets drew
    // `9A −59 / B3 −86 / 9A −64 / 9A −67 / F9 −73` — two of five on weak
    // siblings with both settings demonstrably active. The two-line fix narrows
    // the lottery and does not close it, and the board never roams off its draw,
    // so a bad draw is the association for the whole run.
    //
    // An explicit scan closes it, because the choice is then ours and is
    // printed: the survey line IS the acceptance instrument, and the bar is
    // relative — join the strongest sibling visible in your own boot scan —
    // because every sibling read ~20 dB below its afternoon figure that night
    // and an absolute-dBm bar is the wrong shape for a mesh.
    //
    // Costs, stated: an all-channel scan blocks for roughly 2-4 s at boot, which
    // is why it happens here and NOT on the rejoin path (see supervise() — a
    // scan there would block loopTask for seconds in the middle of an outage,
    // and the 250 ms supervise poll is what recovers the socket). It also
    // allocates, inside the driver and in Arduino's String-returning accessors —
    // at boot, before the first snapshot, so invariant #7's window has not
    // opened yet.
    bool pick_strongest(const char* ssid, std::uint8_t (&bssid)[6],
                        std::int32_t& channel) noexcept;

    // Resolves kAnvilHost on OUR clock, immediately before the connect, and
    // returns how long it took.
    //
    // Two jobs, and the second is the one that will still matter in a month.
    // It warms lwIP's DNS cache, so the lookup inside the connect is a hit. And
    // it SPLITS the 4018 ms that is the largest term in a reconnect: `dns=` on
    // the attempt line against `socket up N ms` on the recovery line says
    // whether the next lever is a resolver problem or a TLS one. Guessing which
    // has already cost this milestone two sessions.
    //
    // It blocks for as long as the resolver takes, which is why it is called
    // only when the station is associated — and, since the rebuild, on the RX
    // task rather than on loopTask, so nothing it does can be felt by the
    // supervisor's poll.
    std::int64_t warm_dns() noexcept;

    // Reads the association's rssi out of the driver, at most every
    // kRssiPeriodUs. Silent when the station is not associated: the driver
    // answers with an error there, and recording its zero would put a 0 dBm
    // sample — a perfect signal — in the middle of an outage.
    void sample_rssi(std::int64_t now) noexcept;

    FramePipe& pipe_;
    LinkQuality& link_;
    // Touched only by the RX task, which is one task, so it needs no
    // synchronisation of its own.
    FrameReassembler<FramePipe> reassembler_;

    WsFrameParser<WsTransport> parser_;

    // The socket, and everything that dies with it. All RX-task-private: the
    // supervisor never touches `tls_`, it only reads `socket_up_` and sets
    // `connect_requested_`.
    esp_tls_t* tls_ = nullptr;
    int fd_ = -1;                     // borrowed from esp-tls, for SO_ERROR
    std::int64_t opened_us_ = 0;
    std::uint64_t socket_bytes_ = 0;
    int close_code_ = -1;             // the server's close frame, if it sent one
    char close_reason_[64] = {};
    std::uint32_t deaths_ = 0;

    // The read buffer, once, in the object. 4 KiB of BSS rather than 4 KiB of
    // task stack — the RX task's stack has to hold an mbedtls handshake and this
    // would be a quarter of it — and never a heap block (invariant #7).
    std::uint8_t rx_buf_[kWsRxBufferBytes] = {};

    // The two words that cross tasks, and the only two.
    //
    // The three words that cross tasks, and the only three.
    //
    // `socket_up_` and `last_data_us_` are written by the RX task and read by
    // supervise() on loopTask; `connect_requested_` is written by supervise()
    // and consumed by the RX task. All three are branched on rather than merely
    // reported, which is the bar that decides what is atomic here while every
    // diagnostic counter in this firmware is not.
    //
    // `last_data_us_` is the silence recycle's input (kSilenceRecycleUs). It is
    // atomic and not a plain int64 for one reason: a torn 64-bit read on this
    // 32-bit core would not merely misreport a number, it would hand the
    // supervisor a garbage age and could tear down a healthy socket. Stamped at
    // the connect as well as at every data chunk, so the value always describes
    // THIS socket and never the one before it.
    //
    // IT COUNTS DATA, NOT BYTES, AND TURNING THE CLIENT PING ON IS WHAT FORCED
    // THAT. This used to be stamped at the read, on any bytes at all, which was
    // right while the only things arriving were data frames. A client ping
    // manufactures a pong every kPingPeriodUs, and a pong is bytes — so under
    // the old rule OUR OWN traffic would have refreshed this clock and the
    // five-minute recycle could never have fired against a peer that answers
    // pongs and publishes nothing. That is not a hypothetical peer: it is
    // precisely the 2026-08-16 00:12 stall, and the recycle is the only recovery
    // path there is. Stamping on data chunks makes the test strictly stronger —
    // "no DATA for five minutes" rather than "no bytes for five minutes" — and
    // restores the meaning the constant was sized against.
    //
    // The stamp is `c.arrival_us`, which is the value the read took BEFORE
    // parsing, so the old comment's property is preserved exactly: a read that
    // costs an unusually long feed still cannot age its own arrival.
    //
    // PRICED RATHER THAN ASSUMED, because 64 bits is not free here: the LX7 has
    // no 64-bit atomic instruction, so `nm` on the linked image shows
    // `__atomic_load_8` and `__atomic_store_8` — ESP-IDF's own newlib
    // implementations, a short critical section each. That is a lock taken ~40
    // times a second on the RX task and 4 times a second on loopTask, tens of
    // cycles apiece, against a read path measured at `feed 0%` of its budget. A
    // 32-bit millisecond stamp would have been lock-free and would have wrapped
    // at 49.7 days, which is the class of ceiling the age clock was just widened
    // out of (ARCHITECTURE §9, 2026-08-16) — not a trade worth repeating to save
    // a spinlock nothing is contending.
    std::atomic<bool> socket_up_{false};
    std::atomic<bool> connect_requested_{false};
    std::atomic<std::int64_t> last_data_us_{0};

    TaskHandle_t rx_task_ = nullptr;

    WsSupervisor supervisor_;

    // The association half, added 2026-08-10 after the bench found the case
    // nothing was watching: an AP that deauthenticates with AUTH_FAIL leaves
    // Arduino's auto-reconnect permanently disarmed (see ws_supervisor.hpp), so
    // `connect_wifi()` being a one-shot at boot meant the panel stayed grey for
    // the rest of the run. The policy is host-tested beside the socket one; this
    // is the platform half and the credentials it needs to act.
    //
    // The two pointers are borrowed, not owned. They come from `secrets.h`,
    // where they are `inline constexpr char[]` with static storage duration, so
    // there is nothing to copy and nothing that can dangle — and copying them
    // into a buffer here would put the Wi-Fi password in a second place.
    WifiSupervisor wifi_supervisor_;
    const char* ssid_ = nullptr;
    const char* password_ = nullptr;

    std::int64_t last_rssi_us_ = 0;
};

}  // namespace depthcharge::fw
