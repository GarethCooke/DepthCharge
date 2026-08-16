// firmware/src/main.cpp — DepthCharge on the ESP32-S3. M3 stage D: the whole object.
//
// The whole object:
//
//   Wi-Fi + TLS WebSocket        FramePipe        Core 0 feed task        SnapshotChannel        Core 1
//   (our RX task, Core 0)  ----------->  parse -> adapt -> book  ----------------->  render task
//        firmware/                              engine/, linked as-is                  (HUB75 DMA + the log)
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

#include "core_idle.hpp"
#include "feed_task.hpp"
#include "frame_pipe.hpp"
#include "heap_probe.hpp"
#include "panel.hpp"
#include "render_task.hpp"
#include "secrets.h"
#include "ws_transport.hpp"

using namespace depthcharge;
using namespace depthcharge::fw;

namespace {

    constexpr const char* kTag = "main";

    // Statically allocated, in this order, and never freed. Between them these
    // objects are the entire steady-state static footprint of the pipeline:
    //
    //   FramePipe        4 x 16 KiB reassembly slots        65,536 B
    //   FeedTask         AnvilAdapter 8,400 + Book 8,552 + staging 1,168
    //   SnapshotChannel  three DisplaySnapshot slots         3,528 B
    //   RenderTask       received_ 1,168 + LadderView ~510
    //
    // All placed before the first packet arrives. Nothing below this line
    // allocates once the tasks are running, which is what invariant #7 asks of
    // the feed path and what heap_probe measures. The panel is the one exception
    // and a deliberate one: its DMA framebuffers are heap, taken once inside
    // Panel::begin() — before the boundary the invariant names ("after connect
    // and first snapshot"), never per frame.
    //
    // The two instruments the stall characterisation added. Neither is in the
    // feed path: `g_idle` is written by the two idle tasks and `g_link` by
    // loopTask, and the feed task and render task only read them.
    CoreIdleProbe g_idle;
    LinkQuality g_link;

    FramePipe g_pipe;
    SnapshotChannel g_channel;
    FeedTask g_feed(g_pipe, g_channel, anvil::kAnvilTicker101, g_idle, g_link);
    WsTransport g_transport(g_pipe, g_link);
    HeapProbe g_heap;
    Panel g_panel;
    RenderTask g_render(g_channel, g_feed, g_pipe, g_heap, g_idle, g_link,
                        g_transport.rx_budget(), g_transport.ping_probe(), g_panel);

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

    // Before the tasks, so the idle counters are already running when the first
    // event lands and the first inter-event window is measured like every other
    // one. The frequency is read from the running system rather than assumed:
    // it scales every idle percentage, and a wrong constant would change none of
    // their shape — the least detectable kind of wrong.
    //
    // Not fatal if it fails. A board that cannot register an idle hook can still
    // do everything stage C proves; it just reports every hole verdict as
    // unknown, which is the honest degradation.
    (void)g_idle.begin(getCpuFrequencyMhz());

    if (!g_pipe.begin()) { halt("could not create the frame pipe queues"); }
    if (!g_feed.start()) { halt("could not start the feed task on core 0"); }

    // THE BOOT ORDER IS A MEMORY DECISION, AND IT IS THE ONE JUDGEMENT CALL IN
    // THIS FILE.
    //
    // The HUB75 framebuffers are internal DMA-capable RAM — 64 KiB of it at
    // eight-bit colour double buffered — and that is the same pool Wi-Fi, lwIP
    // and mbedTLS draw from. Whoever goes first wins, so the order chooses which
    // half degrades when the pool is tight, and the two failures are not
    // comparable: a panel that has to drop a colour bit is cosmetic, and a TLS
    // handshake that cannot allocate is a dead object.
    //
    // So Wi-Fi associates first and the panel sizes itself against what is
    // actually left (panel.cpp measures rather than assumes), while the
    // WebSocket client — the part that allocates a TLS context per handshake for
    // the rest of the run — comes up last, behind Panel::begin()'s deliberately
    // generous reserve.
    //
    // Panel::begin() runs whether or not Wi-Fi came up, and BEFORE the halt
    // below, so a board with a bad secrets.h shows an honest grey RESYNC frame
    // rather than a dark panel that reads as "powered off" (invariant #5).
    const bool wifi_up = g_transport.connect_wifi(kWifiSsid, kWifiPassword);

    // Not fatal. A board that cannot light a panel still runs the feed and still
    // prints the whole acceptance log — the same honest degradation the idle
    // probe takes.
    (void)g_panel.begin();

    // The consumer starts once the panel exists, so its first act is to paint
    // the boot frame; it starts before the socket so the very first published
    // frame — the on-connect snapshot — is already being drained when it lands,
    // and the log shows the Stale->Live transition rather than joining after it.
    if (!g_render.start()) { halt("could not start the render task on core 1"); }

    if (!wifi_up) { halt("wifi did not associate — check firmware/include/secrets.h"); }
    if (!g_transport.start()) { halt("websocket client would not start"); }

    ESP_LOGI(kTag, "setup complete; feed on core 0, render + panel on core 1");
}

void loop() {
    // The feed and the panel both live in pinned tasks, so loopTask does almost
    // nothing — and what is left here is now genuinely small. It used to be the
    // only context in which esp_websocket_client's stop()/start() were safe to
    // call, and it paid for that by blocking on a DNS lookup and a client
    // restart; since the transport owns its socket, supervise() reads two atomic
    // flags, runs two host-tested policies and returns. The ~4 s of blocking
    // connect it used to carry is on the RX task where it belongs.
    //
    // 250 ms, not the 1000 ms this used to be. The supervisor now also owns the
    // reconnect cadence, and its shortest deadline is 2 s — polling at 1 s put up
    // to 50% jitter on the one number the change is meant to move. loopTask is
    // priority 1 and this reads a socket flag and two timestamps, so four
    // wake-ups a second is not a cost worth trading a second of grey panel for.
    g_transport.supervise();
    vTaskDelay(pdMS_TO_TICKS(250));
}
