// firmware/src/main.cpp — DepthCharge on the ESP32-S3. M3 stage C: the feed half.
//
// The whole object, at this stage:
//
//   Wi-Fi + TLS WebSocket        FramePipe        Core 0 feed task        SnapshotChannel        Core 1
//   (esp_websocket_client task) ----------->  parse -> adapt -> book  ----------------->  serial console
//        firmware/                              engine/, linked as-is                     (stage D: HUB75)
//
// Two things are worth noticing about that diagram, because they are the point
// of the milestone rather than incidental:
//
//   1. The middle box is `engine/` — the identical code the host harness runs
//      against a replay file, linked here without a line changed. That is
//      invariant #1 paying out: not a port, a link.
//   2. Every arrow crossing a task boundary is one of exactly two hand-offs,
//      FramePipe and SnapshotChannel, and each has a single writer and a single
//      reader (invariant #8). There is no third path between the tasks.
//
// setup() runs on Arduino's loopTask (Core 1). It brings the network up, then
// creates the two long-lived tasks and gets out of the way; loop() does nothing
// but idle, because all the work belongs to tasks with explicit core affinity
// rather than to whichever core Arduino happened to start on.
#include <Arduino.h>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/snapshot_channel.hpp>

#include "esp_log.h"

#include "feed_task.hpp"
#include "frame_pipe.hpp"
#include "heap_probe.hpp"
#include "secrets.h"
#include "serial_console.hpp"
#include "ws_transport.hpp"

using namespace depthcharge;
using namespace depthcharge::fw;

namespace {

    constexpr const char* kTag = "main";

    // Statically allocated, in this order, and never freed. Between them these
    // objects are the entire steady-state memory footprint of the feed path:
    //
    //   FramePipe        2 x 16 KiB reassembly slots        32,768 B
    //   FeedTask         AnvilAdapter 8,400 + Book 8,552 + staging 1,168
    //   SnapshotChannel  three DisplaySnapshot slots         3,528 B
    //
    // ~54 KiB of the S3's 512 KB internal SRAM, all placed before the first packet
    // arrives. Nothing below this line allocates once the tasks are running, which
    // is what invariant #7 asks of the feed path and what heap_probe measures.
    FramePipe g_pipe;
    SnapshotChannel g_channel;
    FeedTask g_feed(g_pipe, g_channel, anvil::kAnvilTicker101);
    WsTransport g_transport(g_pipe);
    HeapProbe g_heap;
    SerialConsole g_console(g_channel, g_feed, g_pipe, g_heap);

    [[noreturn]] void halt(const char* what) {
        ESP_LOGE(kTag, "FATAL: %s — halting. Reset to retry.", what);
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

}  // namespace

void setup() {
    Serial.begin(115200);
    // The USB-serial link enumerates after boot; without this the first and most
    // useful lines of the log are the ones that get lost.
    delay(300);

    // The runtime half of making this firmware audible. CORE_DEBUG_LEVEL in
    // platformio.ini stops ESP_LOGI/ESP_LOGW being compiled out; this stops
    // esp_log_write filtering them, because the precompiled framework ships
    // CONFIG_LOG_DEFAULT_LEVEL=1 (ERROR). Miss either half and the board looks
    // dead on the monitor while working perfectly — which, at a bench, costs an
    // evening.
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(kTag, "DepthCharge M3 stage C — feed task, no panel yet");
    ESP_LOGI(kTag, "engine: ticker %u, price scale 10^-%d, qty step %lld",
        static_cast<unsigned>(anvil::kAnvilTicker101.id),
        static_cast<int>(anvil::kAnvilTicker101.price_decimals),
        static_cast<long long>(anvil::kAnvilTicker101.qty_step));

    const HeapSample boot = sample_heap();
    ESP_LOGI(kTag, "heap at boot: free=%u largest=%u (internal)",
        static_cast<unsigned>(boot.free_internal),
        static_cast<unsigned>(boot.largest_block_internal));

    if (!g_pipe.begin()) { halt("could not create the frame pipe queues"); }

    // The consumer starts first, so that the very first published frame — the
    // on-connect snapshot — is already being drained when it lands, and the log
    // shows the Stale->Live transition rather than joining after it.
    if (!g_console.start()) { halt("could not start the console task on core 1"); }
    if (!g_feed.start()) { halt("could not start the feed task on core 0"); }

    if (!g_transport.connect_wifi(kWifiSsid, kWifiPassword)) {
        halt("wifi did not associate — check firmware/include/secrets.h");
    }
    if (!g_transport.start()) { halt("websocket client would not start"); }

    ESP_LOGI(kTag, "setup complete; feed on core 0, console on core 1");
}

void loop() {
    // The feed and the panel both live in pinned tasks, so loopTask does almost
    // nothing — but it is the right place for the one job that must NOT run in
    // either: esp_websocket_client's stop()/start() are documented as unsafe
    // from the event handler, and the supervisor calls them. See
    // WsTransport::supervise() for what it is guarding against (a clean
    // server-side close, which auto-reconnect does not cover).
    //
    // 250 ms, not the 1000 ms this used to be. The supervisor now also owns the
    // reconnect cadence, and its shortest deadline is 2 s — polling at 1 s put up
    // to 50% jitter on the one number the change is meant to move. loopTask is
    // priority 1 and this reads a socket flag and two timestamps, so four
    // wake-ups a second is not a cost worth trading a second of grey panel for.
    g_transport.supervise();
    vTaskDelay(pdMS_TO_TICKS(250));
}
