// firmware/src/panel.hpp — the HUB75 half, and the only place Ink becomes light.
//
// M3 stage D. This is the platform side of the split ladder_render.hpp is the
// portable side of: everything about geometry, palette and honesty is host-tested
// in dc_tests, and everything in this file is a call into
// ESP32-HUB75-MatrixPanel-DMA that a desk cannot check.
//
// THE PIN MAP IS ASSIGNED BY NAME, and that is not style. The library's config
// takes them positionally as
//
//     int8_t r1, g1, b1, r2, g2, b2, a, b, c, d, e, lat, oe, clk;
//
// so the last three of a fourteen-element brace list are LAT, OE, CLK — 12, 13,
// 11 for this build, NOT 11, 12, 13. hardware/BRINGUP.md flags it as the trap M2
// stepped over and warns the order is version-dependent. It was checked against
// the vendored 3.0.15 header and is as above; assigning by field name means the
// next version can reorder them and nothing here has to notice.
//
// The library's own ESP32-S3 defaults are DIFFERENT from BRINGUP's map (A=18,
// B=8, C=3, D=42, E=-1, LAT=40, OE=2, CLK=41), so every pin must be set — an
// omitted one is a silently wrong panel, not a build error.
//
// NO_GFX, and it earns its keep twice. The library's base class is Adafruit_GFX
// unless NO_GFX is defined; without it the build pulls Adafruit GFX and BusIO for
// a text path this renderer does not use (ladder_font.hpp owns the font, because
// the 64-row budget has to be checkable on a desk). More importantly, with no
// print() and no setTextColor() there is no way to put anything on this panel
// without naming an Ink — which is what makes invariant #5's palette selection
// structural rather than a rule. NO_GFX leaves exactly the primitives used here:
// drawPixelRGB888, the RGB888 drawFastHLine, fillScreenRGB888, flipDMABuffer,
// setBrightness.
#pragma once

#include <cstdint>

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "ladder_render.hpp"
#include "panel_budget.hpp"

namespace depthcharge::fw {

// Brightness. RECORDED BECAUSE THE BRIEF ASKS FOR THE REASON, AND THE REASON IS
// NOT POWER: M2 measured 2.6 A at full white and 0.25 A at representative ladder
// content against a 5 V/5 A supply, so there is ~2x headroom at 255 and nothing
// here is a power decision. 160/255 is an eye-comfort decision at desk distance —
// a 64x64 panel at arm's length at full brightness is unpleasant to sit beside
// for an evening, which is the actual use. It is one constant, and the bench may
// raise it to 255 for the acceptance photograph with no other consequence.
// Raised 160 -> 224 on 2026-08-10 bench evidence, and the reason is legibility
// rather than mood: the ladder reads well at 160 but the 3x5 header does not,
// and a 3-pixel-wide glyph has no contrast to spare. Still not a power decision
// (M2: 2.6 A full white, 0.25 A representative content, 5 V/5 A supply), and
// still one constant — drop it back if it turns out to be unpleasant to sit
// beside for an evening, which is the only thing 160 was ever protecting.
inline constexpr std::uint8_t kPanelBrightness = 224;

// Colour depth is chosen at run time from a measurement (see panel.cpp). These
// bound the search.
//
// THE CEILING IS 6, NOT THE LIBRARY'S DEFAULT 8, AND THE BENCH IS WHY.
// At depth 8 on this panel the driver cannot reach its 60 Hz minimum without
// collapsing the bottom three colour bits onto one timing slot — its own log
// says so: "lsbMsbTransitionBit of 2 used to achieve refresh rate of 60 Hz.
// Percieved colour depth to the eye may be reduced." At depth 6 it clears 60 Hz
// with transition bit 0, every bit correctly weighted, at the same ~100 Hz.
//
// So depth 8 was buying 16 KiB of internal DMA RAM worth of colour the panel was
// never actually showing. That mattered: the 2026-08-10 run steadied at
// **13,816 bytes of free internal heap with a 4,084 byte largest block** once
// Wi-Fi and TLS were up, against ~124 KB in stage C with no panel. The arithmetic
// is in panel_budget.hpp and pinned against that run's numbers in
// test_panel_budget.cpp.
//
// Below 3 the two-tone grey ramp the stale state depends on starts to band, and
// at that point the second buffer is the better thing to keep.
inline constexpr int kMaxColourDepth = 6;
inline constexpr int kMinDoubleBufferedDepth = 3;
inline constexpr int kMinColourDepth = 2;

// Internal DMA-capable heap held back from the panel, taken BEFORE the WebSocket
// client exists. The panel's colour depth is cosmetic and the feed is not, so
// this errs toward a dimmer ladder rather than a TLS handshake that cannot
// allocate. It has to cover two esp_websocket_client handles' buffers (~10 KiB
// each — the spare-handle reconnect, ARCHITECTURE §9 2026-08-10), one live
// mbedTLS context, the client task's stack, and working slack for a reconnect's
// handshake on a warm heap.
//
// NOW MEASURED, AND THE GUESS WAS NEARLY EXACT — which is luck, and is written
// down so nobody mistakes it for judgement. On the 2026-08-10 run the network
// stack settled at **~86 KB** consumed against this 96 KiB held back, leaving
// 13,816 bytes of free internal heap and a 4,084 byte largest block. That is
// enough, but it is not comfortable: a reconnect rebuilds a TLS context, and the
// only reason it succeeds is that the old one is freed at the drop first (the
// bench saw free jump +54,720 the instant the socket died).
//
// Do NOT lower this without a run that shows the steady-state figure improving
// for some other reason. The lever that actually bought headroom was the colour
// depth ceiling above, which is a cosmetic cost; lowering the reserve trades
// against the feed.
inline constexpr std::uint32_t kReserveInternalBytes = 96u * 1024u;

// What begin() will really take: framebuffer + DMA descriptors + the allocator's
// own bookkeeping. The arithmetic lives in panel_budget.hpp, ESP-IDF-free and
// host-tested against the numbers this board printed, because the first version
// of it counted the framebuffer alone and was wrong by 44% — predicted 65,536
// against 94,468 measured, an error only a conservative reserve concealed.
constexpr std::uint32_t panel_cost_bytes(int depth, bool double_buffered) noexcept {
    return panel_total_bytes(kPanelWidth, kPanelHeight, depth, double_buffered);
}

// Everything the bench needs to close stage D's "known unknowns" in one line.
struct PanelReport {
    bool up = false;
    bool double_buffered = false;
    std::uint8_t colour_depth = 0;
    std::uint8_t brightness = 0;
    std::uint32_t predicted_bytes = 0;   // our arithmetic, before begin()
    std::uint32_t free_before = 0;       // internal DMA-capable, before begin()
    std::uint32_t free_after = 0;        // ...and after: the delta is the truth
    std::uint32_t largest_before = 0;
    std::uint32_t psram_bytes = 0;       // the answer stage C left open
    int refresh_hz = 0;                  // the library's own calculation
};

class Panel {
public:
    // Brings the panel up at the deepest colour it can afford, double buffered.
    // Never fatal: a board that cannot light a panel can still run the feed and
    // print the whole serial evidence, which is the honest degradation and the
    // same rule core_idle.hpp follows.
    bool begin() noexcept;

    bool up() const noexcept { return report_.up; }
    const PanelReport& report() const noexcept { return report_; }

    MatrixPanel_I2S_DMA& driver() noexcept { return dma_; }

    // Present the frame just drawn. On the ESP32-S3 this is NOT a blocking
    // wait — it re-points the tail of the DMA descriptor chain at the other
    // buffer and returns (gdma_lcd_parallel16.cpp:431; the previous-buffer-free
    // spin is commented out in the shipped source). So it costs the render task
    // nothing, and the safety comes from cadence instead: the panel scans at
    // ~100 Hz and this task redraws at most every 33 ms, so the DMA has long
    // since moved onto the presented buffer before anything is written into the
    // one behind it.
    void present() noexcept {
        if (report_.up) { dma_.flipDMABuffer(); }
    }

private:
    MatrixPanel_I2S_DMA dma_;
    PanelReport report_{};
};

// The canvas ladder_render.hpp draws into, and THE ONLY PLACE AN Ink BECOMES AN
// RGB VALUE. It holds the palette by value, chosen once from snap.status where
// it is constructed; the renderer it is handed to cannot express a colour at all.
class PanelCanvas {
public:
    PanelCanvas(MatrixPanel_I2S_DMA& dma, const Palette& palette) noexcept
        : dma_(dma), palette_(palette) {}

    void pixel(int x, int y, Ink ink) noexcept {
        const Rgb c = palette_[ink];
        dma_.drawPixelRGB888(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                             c.r, c.g, c.b);
    }

    void hline(int x, int y, int w, Ink ink) noexcept {
        const Rgb c = palette_[ink];
        dma_.drawFastHLine(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                           static_cast<std::int16_t>(w), c.r, c.g, c.b);
    }

private:
    MatrixPanel_I2S_DMA& dma_;
    Palette palette_;
};

}  // namespace depthcharge::fw
