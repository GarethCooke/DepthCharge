// firmware/src/panel.cpp — see panel.hpp.
#include "panel.hpp"

#include <Arduino.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace depthcharge::fw {
namespace {

constexpr const char* kTag = "panel-hw";

// Internal AND DMA-capable, which is the capability the library actually asks
// for (heap_caps_malloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)). Asking the
// same question the allocator will ask is the difference between a fit
// calculation and a guess: on this part the two happen to be the same pool, and
// on a part where they are not, this stays correct.
constexpr std::uint32_t kDmaCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;

std::uint32_t free_dma_internal() noexcept {
    return static_cast<std::uint32_t>(heap_caps_get_free_size(kDmaCaps));
}

std::uint32_t largest_dma_internal() noexcept {
    return static_cast<std::uint32_t>(heap_caps_get_largest_free_block(kDmaCaps));
}

// THE COLOUR-DEPTH LADDER, AND WHY IT IS A CALCULATION AND NOT A RETRY LOOP.
//
// The obvious shape is `for depth in 8..3: if (begin()) break;`. It is wrong
// here, and the library's own source says so: when a rowBitStruct fails to
// allocate, setupDMA() returns false leaving every buffer it already took
// attached to frame_buffer[].rowBits, above a comment reading
//
//     // TODO: should we release all previous rowBitStructs here???
//
// A second begin() then reserve()s and emplace_back()s MORE rows onto the same
// vector. So a failed attempt does not cost nothing — it costs most of a
// framebuffer, permanently, and the retry beneath it fails for a reason that has
// nothing to do with the depth it was testing. One measurement, one attempt.
//
// The brief's order is preserved: lower the colour depth before giving up the
// second buffer. A shallower ramp costs a ladder almost nothing (it draws twelve
// distinct inks), whereas tearing on a book that redraws 13 times a second is a
// visible defect on a panel whose whole job is to be believed.
struct Choice {
    int depth = 0;
    bool double_buffered = false;
    std::uint32_t bytes = 0;
};

Choice choose_depth(std::uint32_t budget) noexcept {
    for (int d = kMaxColourDepth; d >= kMinDoubleBufferedDepth; --d) {
        const std::uint32_t need = panel_cost_bytes(d, true);
        if (need <= budget) { return Choice{d, true, need}; }
    }
    for (int d = kMaxColourDepth; d >= kMinColourDepth; --d) {
        const std::uint32_t need = panel_cost_bytes(d, false);
        if (need <= budget) { return Choice{d, false, need}; }
    }
    return Choice{};
}

}  // namespace

bool Panel::begin() noexcept {
    report_.free_before = free_dma_internal();
    report_.largest_before = largest_dma_internal();
    report_.psram_bytes = static_cast<std::uint32_t>(ESP.getPsramSize());

    // The PSRAM answer stage C left open, stated where it will be read. Note the
    // panel does NOT use it and must not: the library only places framebuffers in
    // SPIRAM when SPIRAM_DMA_BUFFER is defined, which this build never defines.
    // BOARD_HAS_PSRAM in platformio.ini is about the N16R8 board override, not
    // about this driver.
    ESP_LOGI(kTag, "psram %u B (present=%s, NOT used for the framebuffer)",
             static_cast<unsigned>(report_.psram_bytes),
             report_.psram_bytes > 0 ? "yes" : "no");

    const std::uint32_t budget = report_.free_before > kReserveInternalBytes
                                     ? report_.free_before - kReserveInternalBytes
                                     : 0u;
    const Choice choice = choose_depth(budget);

    // The rungs are printed as TOTAL cost, which is what the fit is decided on.
    // They are larger than the library's own "Allocating N bytes" line by the
    // descriptors and bookkeeping — that gap is the defect this arithmetic was
    // rewritten to close, so the two lines disagreeing is now expected and the
    // amount they disagree by is checkable.
    ESP_LOGI(kTag,
             "dma-internal free=%u largest=%u | reserve=%u budget=%u"
             " | rungs d6=%u d5=%u d4=%u d3=%u (double), d6single=%u",
             static_cast<unsigned>(report_.free_before),
             static_cast<unsigned>(report_.largest_before),
             static_cast<unsigned>(kReserveInternalBytes), static_cast<unsigned>(budget),
             static_cast<unsigned>(panel_cost_bytes(6, true)),
             static_cast<unsigned>(panel_cost_bytes(5, true)),
             static_cast<unsigned>(panel_cost_bytes(4, true)),
             static_cast<unsigned>(panel_cost_bytes(3, true)),
             static_cast<unsigned>(panel_cost_bytes(6, false)));

    if (choice.depth == 0) {
        ESP_LOGE(kTag,
                 "no colour depth fits in %u B — running WITHOUT a panel."
                 " The feed and the serial evidence are unaffected.",
                 static_cast<unsigned>(budget));
        return false;
    }

    HUB75_I2S_CFG cfg;
    cfg.mx_width = kPanelWidth;
    cfg.mx_height = kPanelHeight;
    cfg.chain_length = 1;

    // BY NAME. See the header: the brace-initialiser form puts LAT, OE, CLK in
    // the last three slots and hardware/BRINGUP.md records that as the trap.
    cfg.gpio.r1 = 4;
    cfg.gpio.g1 = 5;
    cfg.gpio.b1 = 6;
    cfg.gpio.r2 = 7;
    cfg.gpio.g2 = 15;
    cfg.gpio.b2 = 16;
    cfg.gpio.a = 17;
    cfg.gpio.b = 18;
    cfg.gpio.c = 8;
    cfg.gpio.d = 9;
    cfg.gpio.e = 10;   // 1/32 scan: a 64x64 panel needs E
    cfg.gpio.lat = 12;
    cfg.gpio.oe = 13;
    cfg.gpio.clk = 11;

    // FM6124, read off the board as `FM 6124D` at M2. FM6126A is the documented
    // fallback if the panel comes up blank or with the top half shifted.
    cfg.driver = HUB75_I2S_CFG::FM6124;
    cfg.double_buff = choice.double_buffered;
    cfg.setPixelColorDepthBits(static_cast<std::uint8_t>(choice.depth));

    // CLOCK PHASE: INVERTED — RE-TESTED 2026-08-14 ON THE OWNED CLIENT, AND THE
    // 2026-08-11 TRADE IS GONE. Two experiments, one per transport, both kept
    // here because they disagree and the disagreement is the finding:
    //
    //   2026-08-11, esp_websocket_client: `false` cleared the header ghosting
    //   and collapsed inbound from ~6 msg/s to ~2 msg/s (sock_gaps=0, both
    //   cores ~96% idle), reproduced both directions — read as the panel
    //   jamming its own radio, so `true` shipped and the ghosting was accepted.
    //
    //   2026-08-14, owned transport, same divider 24, same desk, RSSI -38..-47:
    //   A/B/A at ~10 min per arm against the same morning's 25-min baseline —
    //     true   6.58 msg/s median (25 min)      connects=1 sock_gaps=0
    //     false  6.78 msg/s median, 46.7 KB/s    connects=1 sock_gaps=0
    //     true   6.48 msg/s median, 47.8 KB/s    connects=1 sock_gaps=0
    //   No collapse; every arm sits at the TCP-window byte ceiling
    //   (ARCHITECTURE §9 2026-08-11). The glyph fix is free, so `false` ships.
    //   Logs device-monitor-260814-{151322,154752,155929}.log, analysed with
    //   tools/board_log_lag.py.
    //
    // Which 2026-08-11 variable was real — the old client's socket path or that
    // week's mesh weather — is unresolved and does not block the setting. The
    // mechanism note stands: `clkphase` reaches exactly one thing,
    // `bus_cfg.invert_pclk`, a GPIO-matrix inversion on the CLK pin alone
    // (FM6124 does not override it — only MBI5124 and DP3246 force it true).
    // If inbound ever collapses to ~2 msg/s on a healthy link again, flip this
    // first and put a ten-minute capture through board_log_lag.py before
    // believing any other theory.
    cfg.clkphase = false;

    report_.colour_depth = static_cast<std::uint8_t>(choice.depth);
    report_.double_buffered = choice.double_buffered;
    report_.predicted_bytes = choice.bytes;

    if (!dma_.begin(cfg)) {
        ESP_LOGE(kTag,
                 "begin() failed at depth %d %s (predicted %u B against %u B free)"
                 " — running WITHOUT a panel",
                 choice.depth, choice.double_buffered ? "double-buffered" : "single-buffered",
                 static_cast<unsigned>(choice.bytes),
                 static_cast<unsigned>(report_.free_before));
        return false;
    }

    dma_.setBrightness(kPanelBrightness);
    report_.brightness = kPanelBrightness;
    report_.up = true;
    report_.refresh_hz = dma_.calculated_refresh_rate;
    report_.free_after = free_dma_internal();

    // Both buffers start as the honest state rather than as whatever the DMA
    // memory held: the render task paints the real boot frame within one period,
    // but between begin() and that first draw the panel is on, and "on and
    // black" is the reading invariant #5 spends the rest of this file avoiding.
    const Rgb wash = kStalePalette[Ink::Background];
    dma_.fillScreenRGB888(wash.r, wash.g, wash.b);
    dma_.flipDMABuffer();
    dma_.fillScreenRGB888(wash.r, wash.g, wash.b);

    // Three independent numbers for the same allocation: ours, the measured heap
    // delta, and the library's own "Allocating %d bytes memory for DMA BCM
    // framebuffer(s)" line above this one. They should agree to within the
    // allocator's per-block overhead; if they do not, this arithmetic has drifted
    // from the library's and the bench can see it in one place.
    // The library's `calculated_refresh_rate` is computed from the NOMINAL
    // i2sspeed, which the S3 backend then ignores in favour of a bucketed
    // divider. Print both, and print the divider, so the bench never again reads
    // a refresh figure that is 25% low without knowing it.
    ESP_LOGI(kTag, "pixel clock: nominal %d MHz -> divider %d -> ACTUAL %d.%02d MHz"
                   " | refresh: library says %d Hz, really ~%d Hz",
             static_cast<int>(kI2sClockHz / 1000000), kS3LcdDivNum,
             static_cast<int>(panel_actual_clock_hz() / 1000000),
             static_cast<int>((panel_actual_clock_hz() % 1000000) / 10000),
             dma_.calculated_refresh_rate,
             panel_actual_refresh_hz(kPanelWidth, kPanelHeight, choice.depth));

    ESP_LOGI(kTag,
             "UP: %dx%d depth=%d %s brightness=%u refresh=%d Hz"
             " | predicted=%u measured=%u B | dma-internal free %u -> %u",
             kPanelWidth, kPanelHeight, choice.depth,
             choice.double_buffered ? "double-buffered" : "SINGLE-BUFFERED (tearing possible)",
             static_cast<unsigned>(kPanelBrightness), report_.refresh_hz,
             static_cast<unsigned>(choice.bytes),
             static_cast<unsigned>(report_.free_before - report_.free_after),
             static_cast<unsigned>(report_.free_before),
             static_cast<unsigned>(report_.free_after));

    return true;
}

}  // namespace depthcharge::fw
