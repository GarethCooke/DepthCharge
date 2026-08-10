// firmware/src/ladder_font.hpp — the 3x5 panel font, and why it is ours.
//
// M3 stage D's row budget is the tightest arithmetic in the project: kDisplayLevels
// is 27 a side, so 27 + 1 spread + 27 is 55 of 64 rows before a header exists. The
// brief's instruction if the header does not fit is "draw fewer levels — do NOT
// change kDisplayLevels", because that constant is engine/ and a §5 change.
//
// A font whose metrics we do not control turns that instruction into a bench
// discovery. Adafruit GFX's built-in font is 5x7 in a 6x8 cell (10 characters
// across 64 px, 8 rows for one line); its custom fonts position the cursor on the
// BASELINE rather than the top-left, so the row a line actually occupies depends on
// the glyphs in it. Either way the budget could only be checked by looking at the
// panel.
//
// So the font is 41 glyphs of five bytes, here, and the budget is a static_assert
// in ladder_render.hpp that is checked by `cmake --workflow --preset host` on a
// desk with no board attached. 3x5 in a 4x5 cell gives SIXTEEN characters across
// 64 px and five rows for the header — which is what makes 27 levels a side fit
// exactly, with two rows left over for the tape strip.
//
// Cost: 205 bytes of .rodata. That is the whole price of moving the question off
// the bench, and it also removes the Adafruit GFX text path from the build
// entirely (the panel is compiled with NO_GFX — see panel.hpp), which is a second
// benefit: with no print() there is no way to draw text without naming an Ink.
//
// ESP-IDF-FREE, like frame_reassembler.hpp / gap_histogram.hpp / stall_probe.hpp /
// reject_log.hpp / ws_supervisor.hpp before it. harness/tests/test_ladder_render.cpp
// is where it is proven.
#pragma once

#include <cstddef>
#include <cstdint>

namespace depthcharge::fw {

// One glyph is three columns by five rows. The cell is one column wider, so a
// character advances four pixels and a run of n characters is 4n-1 wide — the
// trailing column of air belongs to the gap, not to the glyph.
inline constexpr int kGlyphWidth = 3;
inline constexpr int kGlyphHeight = 5;
inline constexpr int kGlyphAdvance = kGlyphWidth + 1;

// A row is a 3-bit mask, most significant bit leftmost: 0b101 is "#.#".
using GlyphRow = std::uint8_t;

struct Glyph {
    GlyphRow row[kGlyphHeight];
};

namespace detail {

// Index layout, and it is deliberately dense rather than an ASCII table: the
// alternative (32..90, 59 entries) would be 90 bytes of blanks for characters
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
    {{0, 0, 0, 0, 0}},  // space
    {{7, 5, 5, 5, 7}},  // 0
    {{2, 6, 2, 2, 7}},  // 1
    {{7, 1, 7, 4, 7}},  // 2
    {{7, 1, 7, 1, 7}},  // 3
    {{5, 5, 7, 1, 1}},  // 4
    {{7, 4, 7, 1, 7}},  // 5
    {{7, 4, 7, 5, 7}},  // 6
    {{7, 1, 2, 2, 2}},  // 7
    {{7, 5, 7, 5, 7}},  // 8
    {{7, 5, 7, 1, 7}},  // 9
    {{2, 5, 7, 5, 5}},  // A
    {{6, 5, 6, 5, 6}},  // B
    {{3, 4, 4, 4, 3}},  // C
    {{6, 5, 5, 5, 6}},  // D
    {{7, 4, 6, 4, 7}},  // E
    {{7, 4, 6, 4, 4}},  // F
    {{3, 4, 5, 5, 3}},  // G
    {{5, 5, 7, 5, 5}},  // H
    {{7, 2, 2, 2, 7}},  // I
    {{1, 1, 1, 5, 2}},  // J
    {{5, 5, 6, 5, 5}},  // K
    {{4, 4, 4, 4, 7}},  // L
    {{5, 7, 7, 5, 5}},  // M
    {{5, 7, 5, 5, 5}},  // N
    {{7, 5, 5, 5, 7}},  // O  — identical to '0' at three columns; no word this
                        //      panel draws mixes them ambiguously.
    {{7, 5, 7, 4, 4}},  // P
    {{7, 5, 5, 7, 1}},  // Q
    {{6, 5, 6, 5, 5}},  // R  — B with an open foot; the only difference is row 5.
    {{3, 4, 2, 1, 6}},  // S
    {{7, 2, 2, 2, 2}},  // T
    {{5, 5, 5, 5, 7}},  // U
    {{5, 5, 5, 5, 2}},  // V
    {{5, 5, 7, 7, 5}},  // W
    {{5, 5, 2, 5, 5}},  // X
    {{5, 5, 2, 2, 2}},  // Y
    {{7, 1, 2, 4, 7}},  // Z
    {{0, 0, 0, 0, 2}},  // .
    {{0, 0, 7, 0, 0}},  // -
    {{0, 2, 0, 2, 0}},  // :
    {{7, 1, 3, 0, 2}},  // ?
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

// The 3-bit column mask for one row of one character. `row` outside [0,
// kGlyphHeight) is empty rather than undefined: the drawing loop is bounded by
// the panel, not by the glyph, so a clipped glyph asks for rows it does not have.
constexpr GlyphRow glyph_row(char c, int row) noexcept {
    if (row < 0 || row >= kGlyphHeight) { return 0; }
    return detail::kGlyphs[static_cast<std::size_t>(glyph_index(c))].row[row];
}

// Width in pixels of a NUL-terminated string: 4n-1, because the last character
// contributes no trailing gap. Zero for an empty or null string — a caller
// right-aligning an empty label must land at the right edge, not one pixel past.
constexpr int text_width(const char* s) noexcept {
    if (s == nullptr) { return 0; }
    int n = 0;
    while (s[n] != '\0') { ++n; }
    return n == 0 ? 0 : n * kGlyphAdvance - 1;
}

}  // namespace depthcharge::fw
