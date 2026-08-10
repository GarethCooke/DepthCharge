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

#include <cstdint>

#include "esp_event.h"
#include "esp_websocket_client.h"

#include "frame_pipe.hpp"
#include "frame_reassembler.hpp"

namespace depthcharge::fw {

// Ticker 101 on the deployed demo — the M0/M1/M3 subject, hardcoded for M3 (the
// brief's "one panel, one ticker, one venue"). Multi-ticker is M7.
inline constexpr char kAnvilUri[] = "wss://anvil.garethcooke.com/ws?ticker=101";

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

// The client's own RX buffer, deliberately smaller than an Anvil book frame.
//
// It could be set past 8,726 bytes so that most messages arrive in a single
// event, but that would leave the reassembly path — the code that has to be
// right when a message *is* split — running only rarely, and rarely-run code on
// a device that must not stall is code that rots. At 4 KiB every 8.7 KB book
// frame arrives in three chunks, so reassembly is exercised ~12 times a second
// and any bug in it shows up on the first bench run rather than in a month.
inline constexpr int kWsRxBufferBytes = 4096;

// How long a failed connection attempt waits before the next one.
//
// This is OUR backoff, not the client's, and the distinction is forced on us:
// this vintage of `esp_websocket_client_config_t` has no `reconnect_timeout_ms`
// field (checked member by member in the shipped header — the same gap that put
// a pinned root in anvil_root_ca.hpp instead of the certificate bundle), so the
// library's `WEBSOCKET_RECONNECT_TIMEOUT_MS` is a compile-time 10 s baked into
// the precompiled archive with no way to override it. The supervisor therefore
// preempts that wait rather than configuring it, restarting the client itself
// this long after the socket went down. The client's auto-reconnect stays
// enabled underneath as a backstop and simply never wins the race at 2 s
// against 10 s — on the 2026-08-09 bench it was given 12 s twice and recovered
// neither outage, so "backstop" is the most that can be claimed for it.
//
// 2 s rather than 10 s because an Anvil redeploy is the routine case, not the
// exceptional one. Ten seconds of grey for a server that came back in three
// tells the desk nothing it could act on; it just makes a healthy feed look
// broken, which is the mirror image of the lie invariant #5 exists to prevent.
inline constexpr std::int64_t kReconnectBackoffUs = 2 * 1000 * 1000;

// Measured, not assumed: 2026-08-09 bench, the interval from the supervisor's
// stop/start to the panel reading LIVE again, end to end.
//
// The 18:22 run split it, and the split is the reason this stays conservative
// rather than being tuned down to the one term it now formally bounds:
//
//   ~2545 ms  esp_websocket_client_stop() blocking before the attempt begins
//    4018 ms  DNS + TCP + TLS + upgrade — the only part inside the budget
//    ~435 ms  socket up -> Anvil's snapshot -> LIVE (invariant #5 grants LIVE
//             on data, never on a socket; the server is not the slow part)
//
// Only the middle term runs against kHandshakeBudgetUs, because the stamp is
// taken after stop() returns — so today's real margin is 7000/4018 = 1.74x.
// Held at the end-to-end figure anyway: it is one sample, and the cost of being
// wrong downward is a supervisor that kills connections a beat before they
// succeed, which is the failure mode that cannot self-correct.
inline constexpr std::int64_t kObservedRecoveryUs = 6136 * 1000;

// What one connection attempt is allowed to take end to end: DNS, TCP, the TLS
// handshake against the pinned root, the HTTP upgrade, and the snapshot that
// follows. The supervisor will not disturb an attempt of its own inside this
// window, so understating it is the one way this code can turn a recoverable
// outage into a permanent one — hence the assert, and hence a value picked
// above the worst figure the bench has actually produced rather than the 2.5 s
// cold Wi-Fi-plus-TLS bring-up that the first run made it tempting to use.
inline constexpr std::int64_t kHandshakeBudgetUs = 7 * 1000 * 1000;

static_assert(kHandshakeBudgetUs >= kObservedRecoveryUs,
              "the handshake budget must cover the slowest recovery the bench has measured");

class WsTransport {
public:
    explicit WsTransport(FramePipe& pipe) noexcept
        : pipe_(pipe), reassembler_(pipe, kFrameCapacity) {}

    // Blocks until the station has an IP or `timeout_ms` elapses.
    bool connect_wifi(const char* ssid, const char* password,
                      std::uint32_t timeout_ms = 30000) noexcept;

    // Builds and starts the client. Auto-reconnect is left ON (the IDF default):
    // recovery here is transport-driven, never seq-driven — on reconnect Anvil
    // sends a fresh snapshot and the phase-1 book adopts it (protocol §4).
    bool start() noexcept;

    // Call periodically from a normal task context — NOT from the event handler,
    // which esp_websocket_client.h explicitly forbids for stop()/start().
    //
    // This exists because the client's auto-reconnect does not cover every way a
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
    // It now does a second job as well: it OWNS the retry cadence, because the
    // client's own is not configurable here (see kReconnectBackoffUs). The
    // trigger is this function's own poll of esp_websocket_client_is_connected()
    // and nothing else — deliberately, after a first attempt drove it from the
    // event stream instead and never fired once on the bench. See on_event() for
    // what that cost and why the socket state is the only signal worth trusting.
    void supervise() noexcept;

    bool connected() const noexcept {
        return client_ != nullptr && esp_websocket_client_is_connected(client_);
    }

private:
    static void event_trampoline(void* arg, esp_event_base_t base, std::int32_t id,
                                 void* event_data) noexcept;
    void on_event(std::int32_t id, esp_websocket_event_data_t* data) noexcept;

    // Records that a connection attempt has just begun: counts it, and starts
    // its handshake-immunity clock. Both must happen together at every site that
    // starts one, which is why this exists rather than the two lines — the boot
    // connection in start() forgot them and so got no immunity at all, which the
    // 18:15 bench log's attempt counter gave away only by accident. Defined in
    // the .cpp so the header need not pull in esp_timer.h.
    void note_attempt_begun() noexcept;

    FramePipe& pipe_;
    // Touched only by the WebSocket client's callback, which is a single task,
    // so it needs no synchronisation of its own.
    FrameReassembler<FramePipe> reassembler_;
    esp_websocket_client_handle_t client_ = nullptr;

    // Supervisor state, touched only from supervise()'s caller context — no
    // atomics and no shared flags, because after the 2026-08-09 bench there is
    // nothing left to share: the client's task reports nothing this needs.
    //
    // Two clocks, and the whole correctness argument is the gap between them.
    // `disconnected_since_us_` says when the feed died and schedules the FIRST
    // attempt a backoff later. `attempt_started_us_` says when we last restarted
    // the client, and buys that attempt kHandshakeBudgetUs of immunity — inside
    // that window supervise() does nothing at all. Without the second clock a
    // 2 s cadence would kill every attempt a beat before it succeeded, and an
    // outage that recovers on its own would become one that never recovers.
    //
    // The cost is that attempts after the first come a full cycle apart rather
    // than every 2 s. That is the right trade: the case worth optimising is the
    // server that is already back when we look, and that one is served by the
    // first attempt.
    static constexpr std::int64_t kRetryCycleUs = kReconnectBackoffUs + kHandshakeBudgetUs;
    static_assert(kRetryCycleUs > kHandshakeBudgetUs,
                  "supervisor grace must exceed a full client reconnect or it preempts one");
    std::int64_t disconnected_since_us_ = 0;
    std::int64_t attempt_started_us_ = 0;
    std::uint32_t attempts_ = 0;
};

}  // namespace depthcharge::fw
