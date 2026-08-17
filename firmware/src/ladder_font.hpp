// firmware/src/ladder_font.hpp — the 4x6 panel font, and why it is ours.
//
// M3 stage D's row budget is the tightest arithmetic in the project, and the
// font is what spends it. `kDisplayLevels` is 27 a side, so 27 + 1 spread + 27
// is 55 of 64 rows before a header exists. The brief's instruction if the header
// does not fit is "draw fewer levels — do NOT change kDisplayLevels", because
// that constant is engine/ and a §5 change.
//
// A font whose metrics we do not control turns that instruction into a bench
// discovery. Adafruit GFX's built-in font is 5x7 in a 6x8 cell (ten characters
// across 64 px); its custom fonts position the cursor on the BASELINE rather
// than the top-left, so the row a line actually occupies depends on the glyphs
// in it. Either way the budget could only be checked by looking at the panel.
//
// So the font is 41 glyphs of six bytes, here, and the budget is a static_assert
// in ladder_render.hpp that `cmake --workflow --preset host` checks on a desk
// with no board attached. 246 bytes of .rodata is the whole price of moving the
// question off the bench, and it also removes the Adafruit GFX text path from
// the build entirely (the panel is compiled NO_GFX — see panel.hpp), which is a
// second benefit: with no print() there is no way to draw text without naming
// an Ink.
//
// ============================================================================
// IT WAS 3x5 UNTIL THE BENCH LOOKED AT IT
// ============================================================================
//
// The first version was 3x5 in a 4x5 cell: sixteen characters across, five
// header rows, and 27 levels a side — the budget balanced exactly with two rows
// spare. It rendered, and it was not legible. Two bench runs, on the verdict
// "the numbers in top left and right are no longer really visible", including
// one after brightness went 160 -> 224 to rule that out.
//
// Three columns is simply not enough stroke for a digit at desk distance, and no
// amount of brightness fixes a glyph that is one pixel wide per stroke. 4x6 in a
// 5x6 cell is **78% more lit area** per character. It costs, exactly as the
// brief anticipated:
//
//   * twelve characters across instead of sixteen,
//   * one header row more, and therefore ONE LEVEL PER SIDE — kLevels 27 -> 26,
//   * stale reasons capped at eight characters, so DISCONNECT became NO LINK.
//
// That is the trade the brief pre-authorised, and it is worth taking: a ladder
// with 26 levels a side is indistinguishable from one with 27, and a header
// nobody can read is 6 of 64 rows spent on nothing.
//
// ESP-IDF-FREE, like frame_reassembler.hpp / gap_histogram.hpp / stall_probe.hpp
// / reject_log.hpp / ws_supervisor.hpp / panel_budget.hpp before it.
// harness/tests/test_ladder_render.cpp is where it is proven.
#pragma once

#include <cstddef>
#include <cstdint>

namespace depthcharge::fw {

// One glyph is four columns by six rows. The cell is one column wider, so a
// character advances five pixels and a run of n characters is 5n-1 wide — the
// trailing column of air belongs to the gap, not to the glyph.
inline constexpr int kGlyphWidth = 4;
inline constexpr int kGlyphHeight = 6;
inline constexpr int kGlyphAdvance = kGlyphWidth + 1;

// A row is a 4-bit mask, most significant bit leftmost: 0b1001 is "#..#".
using GlyphRow = std::uint8_t;

struct Glyph {
    GlyphRow row[kGlyphHeight];
};

namespace detail {

// Index layout, and it is deliberately dense rather than an ASCII table: the
// alternative (32..90, 59 entries) would be 108 bytes of blanks for characters
// this panel has no way to be asked to draw. The order below IS the index
// arithmetic in glyph_index(); keep them together.
//
//   0        space
//   1..10    '0'..'9'
//   11..36   'A'..'Z'
//   37       '.'
//   38       '-'
//   39       ':'
//   40       '?'   — also the fallback, because a bench should SEE an
//                    unrenderable character rather than a hole in a word.
inline constexpr int kBlankIndex = 0;
inline constexpr int kDigitIndex = 1;
inline constexpr int kAlphaIndex = 11;
inline constexpr int kDotIndex = 37;
inline constexpr int kDashIndex = 38;
inline constexpr int kColonIndex = 39;
inline constexpr int kUnknownIndex = 40;
inline constexpr std::size_t kGlyphCount = 41;

inline constexpr Glyph kGlyphs[kGlyphCount] = {
    {{0, 0, 0, 0, 0, 0}},         // space
    {{6, 9, 9, 9, 9, 6}},         // 0
    {{2, 6, 2, 2, 2, 7}},         // 1
    {{6, 9, 1, 2, 4, 15}},        // 2
    {{15, 2, 6, 1, 9, 6}},        // 3
    {{3, 5, 9, 15, 1, 1}},        // 4
    {{15, 8, 14, 1, 9, 6}},       // 5
    {{6, 8, 14, 9, 9, 6}},        // 6
    {{15, 1, 2, 2, 4, 4}},        // 7
    {{6, 9, 6, 9, 9, 6}},         // 8
    {{6, 9, 9, 7, 1, 6}},         // 9
    {{6, 9, 9, 15, 9, 9}},        // A
    {{14, 9, 14, 9, 9, 14}},      // B
    {{7, 8, 8, 8, 8, 7}},         // C
    {{14, 9, 9, 9, 9, 14}},       // D
    {{15, 8, 14, 8, 8, 15}},      // E
    {{15, 8, 14, 8, 8, 8}},       // F
    {{7, 8, 8, 11, 9, 7}},        // G
    {{9, 9, 15, 9, 9, 9}},        // H
    {{15, 2, 2, 2, 2, 15}},       // I
    {{1, 1, 1, 1, 9, 6}},         // J
    {{9, 10, 12, 12, 10, 9}},     // K
    {{8, 8, 8, 8, 8, 15}},        // L
    {{9, 15, 15, 9, 9, 9}},       // M
    {{9, 13, 13, 11, 11, 9}},     // N
    {{6, 9, 9, 9, 9, 6}},         // O — identical to '0' at four columns; no
                                  //     word this panel draws mixes them.
    {{14, 9, 9, 14, 8, 8}},       // P
    {{6, 9, 9, 9, 10, 5}},        // Q
    {{14, 9, 9, 14, 10, 9}},      // R
    {{7, 8, 6, 1, 9, 6}},         // S
    {{15, 2, 2, 2, 2, 2}},        // T
    {{9, 9, 9, 9, 9, 6}},         // U
    {{9, 9, 9, 9, 6, 6}},         // V — U with the taper one row earlier
    {{9, 9, 9, 15, 15, 9}},       // W
    {{9, 9, 6, 6, 9, 9}},         // X
    {{9, 9, 6, 2, 2, 2}},         // Y
    {{15, 1, 2, 4, 8, 15}},       // Z
    {{0, 0, 0, 0, 6, 6}},         // .  — two pixels wide, two high, so a decimal point is
                                  //      visible at desk distance
    {{0, 0, 0, 6, 0, 0}},         // -
    {{0, 6, 0, 0, 6, 0}},         // :
    {{6, 9, 1, 2, 0, 2}},         // ?
};

}  // namespace detail

// Lower case folds to upper: the panel has one case, and a caller that formats
// "Resync" should get RESYNC rather than six question marks.
constexpr int glyph_index(char c) noexcept {
    if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
    if (c == ' ') { return detail::kBlankIndex; }
    if (c >= '0' && c <= '9') { return detail::kDigitIndex + (c - '0'); }
    if (c >= 'A' && c <= 'Z') { return detail::kAlphaIndex + (c - 'A'); }
    if (c == '.') { return detail::kDotIndex; }
    if (c == '-') { return detail::kDashIndex; }
    if (c == ':') { return detail::kColonIndex; }
    return detail::kUnknownIndex;
}

// The 4-bit column mask for one row of one character. `row` outside [0,
// kGlyphHeight) is empty rather than undefined: the drawing loop is bounded by
// the panel, not by the glyph, so a clipped glyph asks for rows it does not have.
constexpr GlyphRow glyph_row(char c, int row) noexcept {
    if (row < 0 || row >= kGlyphHeight) { return 0; }
    return detail::kGlyphs[static_cast<std::size_t>(glyph_index(c))].row[row];
}

// Width in pixels of a NUL-terminated string: 5n-1, because the last character
// contributes no trailing gap. Zero for an empty or null string — a caller
// right-aligning an empty label must land at the right edge, not one pixel past.
constexpr int text_width(const char* s) noexcept {
    if (s == nullptr) { return 0; }
    int n = 0;
    while (s[n] != '\0') { ++n; }
    return n == 0 ? 0 : n * kGlyphAdvance - 1;
}

}  // namespace depthcharge::fw
