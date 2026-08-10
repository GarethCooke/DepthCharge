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

#include "esp_event.h"
#include "esp_websocket_client.h"

#include "frame_pipe.hpp"
#include "frame_reassembler.hpp"
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
        : pipe_(pipe), link_(link), reassembler_(pipe, kFrameCapacity) {}

    // Blocks until the station has an IP or `timeout_ms` elapses.
    bool connect_wifi(const char* ssid, const char* password,
                      std::uint32_t timeout_ms = 30000) noexcept;

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

private:
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

    // Resolves kAnvilHost on OUR clock, immediately before handing the connect
    // to the library, and returns how long it took.
    //
    // Two jobs, and the second is the one that will still matter in a month.
    // It warms lwIP's DNS cache, so the client's own lookup inside
    // `esp_transport_connect` is a hit. And it SPLITS the 4018 ms that is now
    // the largest term in a reconnect and has never been decomposed: `dns=` on
    // the restart line against `socket up N ms` on the recovery line says
    // whether the next lever is a resolver problem or a TLS one. Guessing which
    // has already cost this milestone two sessions.
    //
    // It blocks loopTask for as long as the resolver takes, which is the reason
    // it is called only when the station is associated. That is a smaller and
    // better-understood block than the one this design removed.
    std::int64_t warm_dns() noexcept;

    // Reads the association's rssi out of the driver, at most every
    // kRssiPeriodUs. Silent when the station is not associated: the driver
    // answers with an error there, and recording its zero would put a 0 dBm
    // sample — a perfect signal — in the middle of an outage.
    void sample_rssi(std::int64_t now) noexcept;

    FramePipe& pipe_;
    LinkQuality& link_;
    // Touched only by the WebSocket client's callback, which is a single task,
    // so it needs no synchronisation of its own. Both handles' callbacks are
    // that same one task per handle, and they never overlap: a handle that has
    // been retired cannot deliver DATA, because its transport was closed at the
    // abort that retired it.
    FrameReassembler<FramePipe> reassembler_;

    // A and B. Both built in start(), both registered for events, exactly one
    // started at a time.
    static constexpr std::size_t kClientCount = 2;
    esp_websocket_client_handle_t clients_[kClientCount] = {nullptr, nullptr};

    // Which of them is the live one. Written by supervise()'s caller context and
    // read inside the event callback, so it is atomic — unlike every diagnostic
    // counter in this firmware, this one is BRANCHED ON, and a torn read would
    // route a data chunk to the reassembler from a handle that is being retired.
    std::atomic<std::uint8_t> live_{0};

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
