// firmware/src/ws_transport.hpp — Wi-Fi + TLS WebSocket, and nothing else.
//
// Everything ESP-IDF-shaped lives on this side of the line: Wi-Fi, mbed-TLS,
// esp_websocket_client, and the driving of the reassembler. What crosses into
// the rest of the firmware is a FramePipe slot of verbatim wire bytes — which is
// exactly what the harness's TraceReader hands the adapter, and why the engine
// cannot tell the difference (invariant #1).
//
// The reassembly *logic* is deliberately not here: it is in
// frame_reassembler.hpp, free of ESP-IDF so the host suite can test it. This
// class is the adapter between Espressif's event callback and that state
// machine, and it owns no engine state and never touches the book.
#pragma once

// Arduino.h FIRST, and not by accident. esp_websocket_client.h pulls in
// esp_event.h -> esp_netif.h -> lwip/inet.h, which defines INADDR_NONE as a
// macro; Arduino's IPAddress.h then declares `extern IPAddress INADDR_NONE;`
// and the macro rewrites the declaration into a syntax error. Including
// Arduino.h first makes IPAddress.h win, which is the order the framework
// expects. Doing it here rather than in the .cpp means no future includer of
// this header can get it wrong.
#include <Arduino.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

// WHICH WEBSOCKET CLIENT. 1 = ours, over esp-tls; 0 = esp_websocket_client.
//
// The default is ours, and the arm is kept for the same reason `depthcharge-ps`
// and `depthcharge-nopp` were kept: a claim about the library ("its framing is
// what breaks, not the link") is only settled by building both and flashing them
// at the same node on the same evening. Everything ABOVE the client is shared
// between the arms by construction — the same Wi-Fi association code, the same
// scan-then-join, the same supervisors, the same reassembler, the same feed task
// — so a difference in death rate has exactly one candidate.
//
// It goes when the bench acceptance passes (3 h strong-node soak, zero
// errno-silent deaths, zero rejects), and this whole switch goes with it. Until
// then `pio run -e depthcharge-espws` is the control.
#ifndef DC_OWNED_WS
#define DC_OWNED_WS 1
#endif

#if DC_OWNED_WS
#include "esp_tls.h"
#include "ws_frame.hpp"
#else
#include "esp_event.h"
#include "esp_websocket_client.h"
#endif

#include "frame_pipe.hpp"
#include "frame_reassembler.hpp"
#include "rx_budget.hpp"
#include "stall_probe.hpp"
#include "ws_supervisor.hpp"

namespace depthcharge::fw {

// Ticker 101 on the deployed demo — the M0/M1/M3 subject, hardcoded for M3 (the
// brief's "one panel, one ticker, one venue"). Multi-ticker is M7.
inline constexpr char kAnvilUri[] = "wss://anvil.garethcooke.com/ws?ticker=101";

// The same authority, spelled apart, because the DNS warm below resolves it
// itself and this vintage of the client exposes no way to ask it what it parsed
// out of the URI. They must agree; a mismatch shows up as a `dns=` figure that
// is always a cache miss while the socket connects perfectly, which is a
// misleading instrument rather than a broken one — so it is worth an eye on
// every edit of the line above.
inline constexpr char kAnvilHost[] = "anvil.garethcooke.com";
inline constexpr char kAnvilPortText[] = "443";

#if DC_OWNED_WS
// The same authority again, in the pieces an owned client actually connects
// with. kAnvilUri above is now only a log line, which is an improvement worth
// noticing: the mismatch hazard the comment above warns about — a URI the
// library parsed one way and a host we resolved another — cannot exist when
// nothing else parses the URI.
inline constexpr std::uint16_t kAnvilPort = 443;
inline constexpr char kAnvilPath[] = "/ws?ticker=101";
// kAnvilPortText above is this same fact in the spelling the espws arm's URI
// needs; the assert is what keeps an edit to one from making the other lie.
static_assert(kAnvilPort == 443 && kAnvilPortText[0] == '4' && kAnvilPortText[1] == '4' &&
                  kAnvilPortText[2] == '3' && kAnvilPortText[3] == '\0',
              "kAnvilPort and kAnvilPortText must state the same port");

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

// Client->server pings: OFF by default, and that is a measurement waiting to be
// taken rather than a conclusion.
//
// The old client pinged every 10 s with a 20 s pong deadline. The prototype that
// held one socket for 3.7 h / 534 MB pinged never, and the RX watchdog plus the
// supervisor covered every real case the bench produced. What a client ping
// covers that they do not is a half-open TCP connection with the association
// still up — where bytes stop and nothing reports it — and the watchdog greys
// the panel for that in 1 s but cannot make the socket admit it is dead.
//
// So: default off, with `depthcharge-ping` building the other arm at 60 s, and
// the answer is a bench figure rather than an argument. 60 s and not 10: this
// is a keepalive against a dead flow, not a latency probe, and every ping is a
// write on a link whose whole problem was writes into fades.
#ifndef DC_WS_PING_MS
#define DC_WS_PING_MS 0
#endif
inline constexpr std::uint32_t kClientPingMs = DC_WS_PING_MS;

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
#endif  // DC_OWNED_WS

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

#if !DC_OWNED_WS
// WebSocket ping/pong: ON here, OFF in the `depthcharge-nopp` build environment.
//
// The old client's knob, kept with the old client. Its owned-arm successor is
// kClientPingMs above, which is a different question asked of a different
// mechanism — this one turns off machinery we did not write, that one turns on
// machinery we did.
//
// The second experiment run through a two-arm build, after power save. The
// 2026-08-13 pinned A/B run showed this stack dying on a weak node while a
// minimal client (no pings, no pong deadline — firmware/diag/link_autopsy.cpp)
// streamed beside it untouched on the same BSSID at the same RSSI. The client's
// ping/pong machinery is the loudest difference, so it gets the same treatment
// power save got: both arms buildable without editing a line, and the result
// read off the bench rather than argued from the library source.
//
// The OFF arm cannot simply zero either field — this vintage substitutes
// defaults for both, read out of the shipped archive rather than the docs:
// ping_interval_sec 0 becomes 10 s (init+0x54f), and pingpong_timeout_sec 0
// becomes 120 s (init+0x58a), NOT disabled. The honest spellings are a
// day-long ping interval and `disable_pingpong_discon = true`, which the
// object code honours (init+0x545 stores a zero deadline only on that flag).
#ifndef DC_WS_PINGPONG
#define DC_WS_PINGPONG 1
#endif
inline constexpr bool kWsPingPong = (DC_WS_PINGPONG != 0);
#endif  // !DC_OWNED_WS

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
        : pipe_(pipe), link_(link), reassembler_(pipe, kFrameCapacity)
#if DC_OWNED_WS
          ,
          parser_(*this)
#endif
    {
    }

    // Blocks until the station has an IP or `timeout_ms` elapses.
    bool connect_wifi(const char* ssid, const char* password,
                      std::uint32_t timeout_ms = 30000) noexcept;

#if DC_OWNED_WS
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
    // DATA reaching the reassembler.
    //
    // Two things in ws_supervisor.hpp are now describing a library this build
    // does not contain — `kClientSelfExitUs` and the
    // `kReconnectBackoffUs + kRetryCycleUs > kClientSelfExitUs` assert that kept
    // a retry off a handle still asleep. They are deliberately NOT touched: the
    // policy is the host-tested part and the espws arm still needs them, so they
    // go when that arm goes, in one commit, rather than leaving the control
    // built against a supervisor the experiment has already edited.
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
    void on_chunk(const WsChunk& c) noexcept { reassembler_.on_chunk(c); }
    void on_ping(const std::uint8_t* payload, std::uint32_t len) noexcept;
    void on_pong() noexcept { pipe_.count_control(); }
    void on_close(std::uint16_t code, const char* reason, std::uint32_t reason_len) noexcept;
    // Deliberately empty. The parser latches the error and stops reading, and
    // rx_main() acts on it after feed() returns — because the socket cannot be
    // destroyed from inside a callback that is walking a buffer owned by the
    // read that is still on the stack.
    void on_protocol_error(WsProtocolError) noexcept {}
#else
    // Builds BOTH client handles and starts one of them. Recovery here is
    // transport-driven, never seq-driven — on reconnect Anvil sends a fresh
    // snapshot and the phase-1 book adopts it (protocol §4).
    //
    // Two handles, and this is the change the 2026-08-10 measurement forced.
    // A dropped socket puts the library's task to sleep for 5 s (see
    // ws_supervisor.hpp), and the only ways to get a live socket back before it
    // wakes are to wait for it — `esp_websocket_client_stop()` blocks on exactly
    // that — or to not need it. So there are two clients, both built once here,
    // and a reconnect starts the one that is idle while the other expires on its
    // own clock. Nothing blocks; the whole 5 s leaves the grey path.
    //
    // The costs, in full: ~10 KiB of heap for the spare's rx/tx buffers, taken
    // once at boot and never in steady state (invariant #7 untouched); one extra
    // 6 KiB task stack for the ~5 s the sleeper overlaps the new connection; and
    // `disable_auto_reconnect = true`, without which the sleeper would wake at
    // 10 s and open a SECOND live socket to Anvil behind our back. Only one TLS
    // context is ever live, because `esp_websocket_client_abort_connection()`
    // closes the transport at the drop — which the 2026-08-09 bench saw as
    // `free=172708 (+47780)` the instant the socket died.
    bool start() noexcept;

    // Call periodically from a normal task context — NOT from the event handler,
    // which esp_websocket_client.h explicitly forbids for stop()/start().
    //
    // This exists because the client's own recovery does not cover every way a
    // socket ends. On a CLEAN server-side close, esp_websocket_client's task
    // sets WEBSOCKET_STATE_CLOSING, echoes the close frame, then breaks out of
    // its `while (client->run)` loop and calls vTaskDelete(NULL). auto_reconnect
    // is only consulted in WEBSOCKET_STATE_WAIT_TIMEOUT, which is reached solely
    // from the abort/error path — so a clean close leaves the client dead with
    // no task and nothing to restart it.
    //
    // Invariant #5 survives that on its own: either CLOSED becomes
    // Gap{Disconnect}, or the RX watchdog fires, and either way the panel greys
    // honestly. But it greys FOREVER, and on a bench that reads as an
    // intermittent hang hours into a healthy run — the hardest possible thing to
    // diagnose. Anvil is redeployed from time to time, so this is not a
    // hypothetical.
    //
    // The decision of WHEN is not here: it is WsSupervisor, which is host-tested.
    // What is here is the platform half — which handle to open, the DNS warm,
    // and the logging. The trigger is this function's own poll of
    // esp_websocket_client_is_connected() and nothing else — deliberately, after
    // a first attempt drove it from the event stream instead and never fired
    // once on the bench. See on_event() for what that cost.
    void supervise() noexcept;

    bool connected() const noexcept {
        const esp_websocket_client_handle_t c = clients_[live_.load(std::memory_order_relaxed)];
        return c != nullptr && esp_websocket_client_is_connected(c);
    }
#endif  // DC_OWNED_WS

private:
#if DC_OWNED_WS
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

private:
    RxBudget budget_;

    // Writes `len` bytes or reports failure; esp_tls_conn_write may take fewer.
    bool write_all(const void* data, std::size_t len) noexcept;

    // Sends a client ping if kClientPingMs says one is due. False means the
    // write failed, the socket has been closed, and the caller must not read it.
    bool maybe_ping(std::int64_t now) noexcept;
#else
    static void event_trampoline(void* arg, esp_event_base_t base, std::int32_t id,
                                 void* event_data) noexcept;
    void on_event(std::int32_t id, esp_websocket_event_data_t* data) noexcept;

    // Starts the idle handle and makes it the live one. Returns false if the
    // library refused, which happens when the idle handle's previous task has
    // not exited yet — `esp_websocket_client_start()` rejects any handle whose
    // state is still >= INIT. The supervisor's constants are asserted to make
    // that rare rather than impossible, so it is reported and retried, never
    // assumed away.
    bool open_spare(const SupervisorDecision& d) noexcept;
#endif

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

#if DC_OWNED_WS
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
    std::int64_t last_ping_us_ = 0;   // client->server, only when kClientPingMs > 0

    // The read buffer, once, in the object. 4 KiB of BSS rather than 4 KiB of
    // task stack — the RX task's stack has to hold an mbedtls handshake and this
    // would be a quarter of it — and never a heap block (invariant #7).
    std::uint8_t rx_buf_[kWsRxBufferBytes] = {};

    // The two words that cross tasks, and the only two.
    //
    // `socket_up_` is written by the RX task and read by supervise() on
    // loopTask; `connect_requested_` is written by supervise() and consumed by
    // the RX task. Both are branched on rather than merely reported, which is
    // the same bar the old `live_` had to clear and the reason they are atomic
    // while every diagnostic counter in this firmware is not.
    std::atomic<bool> socket_up_{false};
    std::atomic<bool> connect_requested_{false};

    TaskHandle_t rx_task_ = nullptr;
#else
    // A and B. Both built in start(), both registered for events, exactly one
    // started at a time.
    static constexpr std::size_t kClientCount = 2;
    esp_websocket_client_handle_t clients_[kClientCount] = {nullptr, nullptr};

    // Which of them is the live one. Written by supervise()'s caller context and
    // read inside the event callback, so it is atomic — unlike every diagnostic
    // counter in this firmware, this one is BRANCHED ON, and a torn read would
    // route a data chunk to the reassembler from a handle that is being retired.
    std::atomic<std::uint8_t> live_{0};
#endif

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
