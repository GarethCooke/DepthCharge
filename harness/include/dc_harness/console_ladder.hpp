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

#include <depthcharge/display_snapshot.hpp>

namespace dc::harness {

struct LadderStyle {
    bool color = true;          // ANSI SGR; off for goldens and dumb terminals
    bool unicode = true;        // box drawing + block bars; off for ASCII-only
    std::size_t levels = 12;    // levels per side to draw (<= kDisplayLevels)
    std::size_t bar_width = 18;
    bool show_tape = true;
};

// One frame, ready to print (ends with a newline).
std::string render_ladder(const depthcharge::DisplaySnapshot& snap, const LadderStyle& style);

// Cursor home + clear, for --follow. Empty when style.color is false, so piped
// output stays clean.
std::string ladder_home(const LadderStyle& style);

// Price/qty helpers shared with the replay report. Integer-only (invariant #3).
std::string format_px(depthcharge::PriceTicks px, std::int32_t decimals);

}  // namespace dc::harness
