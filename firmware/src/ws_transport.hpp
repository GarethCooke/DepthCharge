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

// The client's own RX buffer, deliberately smaller than an Anvil book frame.
//
// It could be set past 8,726 bytes so that most messages arrive in a single
// event, but that would leave the reassembly path — the code that has to be
// right when a message *is* split — running only rarely, and rarely-run code on
// a device that must not stall is code that rots. At 4 KiB every 8.7 KB book
// frame arrives in three chunks, so reassembly is exercised ~12 times a second
// and any bug in it shows up on the first bench run rather than in a month.
inline constexpr int kWsRxBufferBytes = 4096;

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
    void supervise() noexcept;

    bool connected() const noexcept {
        return client_ != nullptr && esp_websocket_client_is_connected(client_);
    }

private:
    static void event_trampoline(void* arg, esp_event_base_t base, std::int32_t id,
                                 void* event_data) noexcept;
    void on_event(std::int32_t id, esp_websocket_event_data_t* data) noexcept;

    FramePipe& pipe_;
    // Touched only by the WebSocket client's callback, which is a single task,
    // so it needs no synchronisation of its own.
    FrameReassembler<FramePipe> reassembler_;
    esp_websocket_client_handle_t client_ = nullptr;

    // Supervisor state, touched only from supervise()'s caller context.
    // The grace period must exceed the client's OWN reconnect backoff
    // (WEBSOCKET_RECONNECT_TIMEOUT_MS, 10 s in this vintage) or the supervisor
    // would fight the mechanism it is backing up: a normal error-path
    // reconnection must be allowed to complete on its own.
    static constexpr std::int64_t kReviveAfterUs = 20 * 1000 * 1000;
    std::int64_t disconnected_since_us_ = 0;
    std::uint32_t revivals_ = 0;
};

}  // namespace depthcharge::fw
