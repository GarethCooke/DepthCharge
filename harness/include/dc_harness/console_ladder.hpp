// dc_harness/console_ladder.hpp — a DisplaySnapshot, drawn in a terminal.
//
// This is the host preview of the HUB75 panel, and it exists to make one thing
// impossible to fake: invariant #5, "a frozen ladder that looks live is the one
// unacceptable output". The stale rendering therefore differs from the live one
// in *three independent channels* — a banner word, the bar glyph, and colour —
// so it survives a monochrome terminal, a colour-blind reader, and a golden test
// that only ever sees plain text.
//
// Aesthetics are the session's call (ARCHITECTURE §8). The layout mirrors the
// panel budget: header row, 27 ask rows, a spread row, 27 bid rows, a tape row.
// Rendering is the display edge, so it may allocate freely (it returns a
// std::string) — but it still formats prices with the engine's integer
// formatter, never a float, so the ladder cannot disagree with the book.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <depthcharge/display_snapshot.hpp>

namespace dc::harness {

struct LadderStyle {
    bool color = true;          // ANSI SGR; off for goldens and dumb terminals
    bool unicode = true;        // box drawing + block bars; off for ASCII-only
    std::size_t levels = 12;    // levels per side to draw (<= kDisplayLevels)
    std::size_t bar_width = 18;
    bool show_tape = true;

    // WHAT THE HEADER CALLS THIS FEED. A style field rather than a
    // DisplaySnapshot field, and that is the decision rather than a shortcut.
    //
    // The venue name was the literal " ANVIL " in the renderer until M4 stage
    // B1, which was true for three milestones and became a lie the evening the
    // second adapter linked. The obvious fix — put the name in DisplaySnapshot —
    // was refused: that struct is copied through the feed->render mailbox on
    // every publish (invariant #4) and is deliberately flat and trivially
    // copyable (#7), so a name in it is bytes on the hot path for something only
    // the display edge reads. And the panel does not want a string at all: at
    // 64x64 the venue is a glyph, decided at stage D.
    //
    // So the caller supplies it. The harness reads the trace's venue tag; the
    // firmware will supply its one compiled-in venue. Empty means draw no venue
    // field at all, which is what a caller that has nothing true to say should
    // do rather than defaulting to a venue it has not checked.
    std::string_view venue{};

    // The instrument, as the venue spells it — "BTC/USD". Empty falls back to
    // the integer `symbol.id` the snapshot carries, which is what Anvil's
    // ticker 101 wants and what a string pair cannot be reduced to.
    std::string_view symbol{};
};

// One frame, ready to print (ends with a newline).
std::string render_ladder(const depthcharge::DisplaySnapshot& snap, const LadderStyle& style);

// Cursor home + clear, for --follow. Empty when style.color is false, so piped
// output stays clean.
std::string ladder_home(const LadderStyle& style);

// Price/qty helpers shared with the replay report. Integer-only (invariant #3).
std::string format_px(depthcharge::PriceTicks px, std::int32_t decimals);

}  // namespace dc::harness
