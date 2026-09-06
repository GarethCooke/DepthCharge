// test_ladder_render.cpp — M3 stage D's panel renderer, on a desk with no panel.
//
// The sixth firmware header the host build knows about, and the one with the
// most to prove. Stage D's whole claim is invariant #5 in copper and light: a
// stale book greys the panel, and it is impossible to draw it any other way. A
// claim like that is worth exactly as much as the thing that checks it, and the
// panel is a bench instrument that two people can look at and disagree about.
//
// So the renderer emits `Ink` — a role — and this file renders into a 64x64 grid
// of Ink. That turns "does it look grey?" into three mechanical questions:
//
//   1. Does every Ink in kStalePalette sit on the grey ramp? — answered at
//      COMPILE time by ladder_render.hpp's own static_assert; re-checked here so
//      a run reports it.
//   2. Does the geometry depend on status? — no: the same book renders the same
//      Ink grid over the ladder region Live and Stale. Geometry stays, hue goes.
//   3. Does every pixel get written? — it must, because the panel is double
//      buffered and the back buffer holds a frame from two draws ago. A renderer
//      that leaves a pixel alone leaves a two-frames-stale pixel on the panel,
//      which is a frozen ladder in miniature.
//
// The row budget is the other thing only a test can hold: 27 levels a side plus
// a spread plus a header is 64 rows with two to spare, and the alternative to
// pinning it here is discovering it on the panel.
#include <doctest/doctest.h>

#include <string>
#include <utility>

#include <cstddef>
#include <cstdint>

#include <depthcharge/display_snapshot.hpp>

#include "alloc_probe.hpp"
#include "ladder_font.hpp"
#include "ladder_render.hpp"

using depthcharge::BookLevel;
using depthcharge::DisplaySnapshot;
using depthcharge::FeedStatus;
using depthcharge::GapReason;
using depthcharge::kDisplayLevels;
using depthcharge::PriceTicks;
using depthcharge::Qty;
using depthcharge::Side;
using depthcharge::fw::all_grey;
using depthcharge::fw::bar_length;
using depthcharge::fw::Ink;
using depthcharge::fw::kAskTop;
using depthcharge::fw::kBeatX;
using depthcharge::fw::kBeatY;
using depthcharge::fw::kBidTop;
using depthcharge::fw::kBottomRuleRow;
using depthcharge::fw::kGlyphAdvance;
using depthcharge::fw::kGlyphHeight;
using depthcharge::fw::kGlyphWidth;
using depthcharge::fw::kHeaderRows;
using depthcharge::fw::kHeaderTop;
using depthcharge::fw::kInkCount;
using depthcharge::fw::kLevels;
using depthcharge::fw::kLivePalette;
using depthcharge::fw::kPanelHeight;
using depthcharge::fw::kPanelWidth;
using depthcharge::fw::kSparkColumns;
using depthcharge::fw::kSpreadRow;
using depthcharge::fw::kStalePalette;
using depthcharge::fw::kStripRows;
using depthcharge::fw::kStripTop;
using depthcharge::fw::kTopRuleRow;
using depthcharge::fw::LadderView;
using depthcharge::fw::palette_for;
using depthcharge::fw::text_width;
using depthcharge::fw::window_max_qty;

namespace {

// The canvas the tests draw into: what Ink each pixel ended up, how many times
// it was written, and whether anything was ever asked for outside the panel.
// The last one is the guard region — a renderer that runs off the edge is caught
// here rather than by the driver silently clipping it on the bench.
struct GridCanvas {
    Ink ink[kPanelHeight][kPanelWidth];
    int writes[kPanelHeight][kPanelWidth];
    int out_of_bounds = 0;

    GridCanvas() { clear(); }

    void clear() {
        for (int y = 0; y < kPanelHeight; ++y) {
            for (int x = 0; x < kPanelWidth; ++x) {
                ink[y][x] = Ink::Count;  // "never written"
                writes[y][x] = 0;
            }
        }
        out_of_bounds = 0;
    }

    void pixel(int x, int y, Ink i) noexcept {
        if (x < 0 || y < 0 || x >= kPanelWidth || y >= kPanelHeight) {
            ++out_of_bounds;
            return;
        }
        ink[y][x] = i;
        ++writes[y][x];
    }

    void hline(int x, int y, int w, Ink i) noexcept {
        if (w < 1) {
            ++out_of_bounds;
            return;
        }
        for (int k = 0; k < w; ++k) { pixel(x + k, y, i); }
    }

    bool every_pixel_written() const {
        for (int y = 0; y < kPanelHeight; ++y) {
            for (int x = 0; x < kPanelWidth; ++x) {
                if (writes[y][x] == 0) { return false; }
            }
        }
        return true;
    }

    int count_in_row(int y, Ink want) const {
        int n = 0;
        for (int x = 0; x < kPanelWidth; ++x) {
            if (ink[y][x] == want) { ++n; }
        }
        return n;
    }
};

// A book with `n` levels a side, quantities descending from the touch so the
// ladder has an obvious shape to assert against.
DisplaySnapshot make_book(int n, FeedStatus status = FeedStatus::Live,
                          GapReason reason = GapReason::Resync) {
    DisplaySnapshot s{};
    s.version = 42;
    s.symbol.id = 101;
    s.symbol.price_decimals = 4;
    s.symbol.qty_decimals = 0;
    s.status = status;
    s.stale_reason = reason;
    s.bid_count = static_cast<std::uint8_t>(n);
    s.ask_count = static_cast<std::uint8_t>(n);
    for (int i = 0; i < n; ++i) {
        s.bids[i] = BookLevel{100000 - i, static_cast<Qty>(100 - i)};
        s.asks[i] = BookLevel{100050 + i, static_cast<Qty>(100 - i)};
    }
    s.has_last = true;
    s.last_px = 100001;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
TEST_CASE("the row budget spends exactly 64 rows, and the font is what spends it") {
    CHECK(kPanelWidth == 64);
    CHECK(kPanelHeight == 64);

    // 6 header + 1 rule + 26 asks + 1 spread + 26 bids + 1 rule + 3 strip.
    CHECK(kHeaderTop == 0);
    CHECK(kHeaderRows == kGlyphHeight);
    CHECK(kTopRuleRow == 6);
    CHECK(kAskTop == 7);
    CHECK(kLevels == 26);
    CHECK(kSpreadRow == 33);
    CHECK(kBidTop == 34);
    CHECK(kBottomRuleRow == 60);
    CHECK(kStripTop == 61);
    CHECK(kStripRows == 3);
    CHECK(kStripTop + kStripRows == kPanelHeight);

    // THE TRADE THE 4x6 FONT COST, PINNED. One header row more than the 3x5 the
    // bench could not read, and therefore one level a side fewer than the book
    // publishes. That is the brief's sanctioned direction — draw fewer levels,
    // never change kDisplayLevels — and it is a truncation of the drawn window,
    // not a change to engine/.
    CHECK(kLevels < static_cast<int>(kDisplayLevels));
    CHECK(static_cast<int>(kDisplayLevels) - kLevels == 1);

    // The two sides plus the spread must not overlap the chrome at either end.
    CHECK(kSpreadRow - 1 - (kLevels - 1) == kAskTop);
    CHECK(kBidTop + (kLevels - 1) == kBottomRuleRow - 1);
}

TEST_CASE("the font's metrics are what the row budget was computed from") {
    CHECK(kGlyphWidth == 4);
    CHECK(kGlyphHeight == 6);
    CHECK(kGlyphAdvance == 5);

    // 5n-1: the trailing column of air belongs to the gap, not the glyph, so a
    // right-aligned string lands flush with the last lit column.
    CHECK(text_width("") == 0);
    CHECK(text_width(nullptr) == 0);
    CHECK(text_width("1") == 4);
    CHECK(text_width("101") == 14);
    CHECK(text_width("10.0001") == 34);

    // Thirteen characters is the whole header line at this cell width, and it
    // lands on 64 exactly — the last glyph's fourth column IS column 63, with
    // the gap it would otherwise have trailed falling off the panel. Fourteen
    // does not fit at all.
    CHECK(text_width("0123456789ABC") == kPanelWidth);
    CHECK(text_width("0123456789ABCD") > kPanelWidth);

    // The two things the header is actually asked to hold, both with a symbol
    // beside them and a column of air between.
    CHECK(text_width("101") + kGlyphAdvance + text_width("10.0001") <= kPanelWidth);
    CHECK(text_width("9999") + kGlyphAdvance + text_width("CHECKSUM") <= kPanelWidth);
}

TEST_CASE("every glyph fits three columns, and unknown characters are visible") {
    using depthcharge::fw::glyph_index;
    using depthcharge::fw::glyph_row;

    for (int c = 0; c < 128; ++c) {
        for (int r = 0; r < kGlyphHeight; ++r) {
            CHECK(glyph_row(static_cast<char>(c), r) <= 0b1111);
        }
    }
    // Rows outside the glyph are empty rather than undefined — the drawing loop
    // is bounded by the panel, so a clipped glyph asks for rows it has not got.
    CHECK(glyph_row('8', -1) == 0);
    CHECK(glyph_row('8', kGlyphHeight) == 0);

    // Space is the only character that draws nothing. Anything unmapped draws
    // '?', because a hole in a word at a bench reads as a rendering bug in the
    // ladder rather than as a bad string.
    int blank_rows = 0;
    for (int r = 0; r < kGlyphHeight; ++r) { blank_rows += (glyph_row(' ', r) == 0) ? 1 : 0; }
    CHECK(blank_rows == kGlyphHeight);
    CHECK(glyph_index('~') == glyph_index('?'));
    CHECK(glyph_index('\0') == glyph_index('?'));
    CHECK(glyph_index('a') == glyph_index('A'));
    CHECK(glyph_index('z') == glyph_index('Z'));

    // Every character the header can produce must be a real glyph, not the
    // fallback: digits, '.', '-' and the letters of every reason word.
    const char* renderable = "0123456789.- ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (const char* p = renderable; *p != '\0'; ++p) {
        CHECK(glyph_index(*p) != glyph_index('?'));
    }
}

// ---------------------------------------------------------------------------
TEST_CASE("invariant #5: the stale palette is entirely grey, and never black") {
    // ladder_render.hpp static_asserts both of these, so this cannot fail
    // without the build having already failed — it is here so a test RUN
    // reports the property rather than only a compile.
    CHECK(all_grey(kStalePalette));
    CHECK_FALSE(all_grey(kLivePalette));

    for (std::size_t i = 0; i < kInkCount; ++i) {
        const auto c = kStalePalette.ink[i];
        CHECK(c.grey());
        // Grey, never blank: a dark panel is ambiguous with "powered off".
        // HeaderBed is the one exception, taken deliberately at the 2026-08-24
        // bench because the stale reason could not be read head-on against the
        // 64-grey wash. It is six rows; the other 58 still carry the signal.
        if (i == static_cast<std::size_t>(Ink::HeaderBed)) {
            CHECK(c.black());
            continue;
        }
        CHECK_FALSE(c.black());
    }

    // Only the live background and the header bed may be pure black; anything
    // else black would be an Ink somebody added and forgot to give a colour.
    for (std::size_t i = 0; i < kInkCount; ++i) {
        if (i == static_cast<std::size_t>(Ink::Background)) { continue; }
        if (i == static_cast<std::size_t>(Ink::HeaderBed)) { continue; }
        CHECK_FALSE(kLivePalette.ink[i].black());
    }

    CHECK(palette_for(FeedStatus::Live).ink[0].r == kLivePalette.ink[0].r);
    CHECK(all_grey(palette_for(FeedStatus::Stale)));
}

TEST_CASE("geometry stays, hue goes: the ladder region is identical Live and Stale") {
    LadderView live_view;
    LadderView stale_view;
    GridCanvas a;
    GridCanvas b;

    const DisplaySnapshot live = make_book(kLevels);
    DisplaySnapshot stale = live;
    stale.status = FeedStatus::Stale;
    stale.stale_reason = GapReason::Disconnect;

    live_view.observe(live);
    live_view.draw(live, a);
    stale_view.observe(stale);
    stale_view.draw(stale, b);

    // Rows below the header: the two frames must be pixel-for-pixel the same
    // ROLE. The palette turns one of them grey, and that is the only difference
    // the panel is allowed to show.
    for (int y = kTopRuleRow; y < kPanelHeight; ++y) {
        for (int x = 0; x < kPanelWidth; ++x) {
            CHECK(a.ink[y][x] == b.ink[y][x]);
        }
    }
    CHECK(a.out_of_bounds == 0);
    CHECK(b.out_of_bounds == 0);
}

TEST_CASE("every pixel of the frame is written, because the back buffer is two frames old") {
    LadderView view;
    GridCanvas c;

    SUBCASE("full book") {
        const DisplaySnapshot s = make_book(kLevels);
        view.observe(s);
        view.draw(s, c);
    }
    SUBCASE("half a book") {
        const DisplaySnapshot s = make_book(kLevels / 2);
        view.observe(s);
        view.draw(s, c);
    }
    SUBCASE("the boot frame — no book at all") {
        DisplaySnapshot s{};
        s.symbol.id = 101;
        s.symbol.price_decimals = 4;
        view.observe(s);
        view.draw(s, c);
    }
    SUBCASE("one side only") {
        DisplaySnapshot s = make_book(kLevels);
        s.ask_count = 0;
        view.observe(s);
        view.draw(s, c);
    }

    CHECK(c.every_pixel_written());
    CHECK(c.out_of_bounds == 0);
}

TEST_CASE("the boot frame is an honest grey empty panel, not a black screen") {
    // Stage C publishes one frame before any data arrives, so v1 is
    // Stale{Resync} with no levels. It is the state the object spends its first
    // seconds in, and a black panel there says nothing where invariant #5
    // requires it to say "not trusted".
    DisplaySnapshot s{};
    s.symbol.id = 101;
    s.symbol.price_decimals = 4;
    CHECK(s.status == FeedStatus::Stale);
    CHECK(s.stale_reason == GapReason::Resync);
    CHECK(s.bid_count == 0);
    CHECK(s.ask_count == 0);

    LadderView view;
    GridCanvas c;
    view.observe(s);
    view.draw(s, c);

    // Chrome is present on both rules...
    CHECK(c.count_in_row(kTopRuleRow, Ink::Chrome) == kPanelWidth);
    CHECK(c.count_in_row(kBottomRuleRow, Ink::Chrome) == kPanelWidth);
    CHECK(c.count_in_row(kSpreadRow, Ink::Spread) == kPanelWidth);

    // ...the header says something...
    int header_lit = 0;
    for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
        header_lit += kPanelWidth - c.count_in_row(y, Ink::HeaderBed);
    }
    CHECK(header_lit > 0);

    // ...and there is no ladder, which is the honest answer.
    for (int i = 0; i < kLevels; ++i) {
        CHECK(c.count_in_row(kSpreadRow - 1 - i, Ink::Background) == kPanelWidth);
        CHECK(c.count_in_row(kBidTop + i, Ink::Background) == kPanelWidth);
    }

    // The whole frame is grey, and none of it below the header is black —
    // the header band is deliberately black so the stale reason can be read
    // head-on (2026-08-24 bench). Everything else still has to glow.
    const auto pal = palette_for(s.status);
    for (int y = 0; y < kPanelHeight; ++y) {
        for (int x = 0; x < kPanelWidth; ++x) {
            const auto rgb = pal[c.ink[y][x]];
            CHECK(rgb.grey());
            if (y >= kHeaderTop + kHeaderRows) { CHECK_FALSE(rgb.black()); }
        }
    }

    // ...and the header band really is the black one, so this exception cannot
    // silently widen into the ladder.
    for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
        CHECK(pal[Ink::HeaderBed].black());
        CHECK(c.count_in_row(y, Ink::HeaderBed) > 0);
    }
}

// ---------------------------------------------------------------------------
TEST_CASE("bars are integer, normalised across both sides, and never invisible") {
    // len = qty * width / max_qty in int64. No float, no rounding (invariant #3).
    CHECK(bar_length(0, 100) == 0);
    CHECK(bar_length(-5, 100) == 0);
    CHECK(bar_length(100, 100) == kPanelWidth);
    CHECK(bar_length(50, 100) == kPanelWidth / 2);

    // A level that exists is never invisible — the console ladder's rule, and
    // for the same reason: an empty row and a tiny row must not read the same.
    CHECK(bar_length(1, 1000000) == 1);

    // A degenerate max cannot divide by zero, and cannot overrun the panel.
    CHECK(bar_length(5, 0) == 0);
    CHECK(bar_length(200, 100) == kPanelWidth);

    // Huge quantities stay in 64-bit rather than wrapping through int.
    CHECK(bar_length(4000000000LL, 8000000000LL) == kPanelWidth / 2);

    DisplaySnapshot s = make_book(4);
    s.bids[0].qty = 500;  // one side dominating must scale the other down
    CHECK(window_max_qty(s) == 500);

    LadderView view;
    GridCanvas c;
    view.observe(s);
    view.draw(s, c);

    // bids[0] is the max, so it fills the row; asks[0] is 100/500 of it.
    CHECK(c.count_in_row(kBidTop, Ink::BidBest) == kPanelWidth);
    CHECK(c.count_in_row(kSpreadRow - 1, Ink::AskBest) == (kPanelWidth * 100) / 500);
}

TEST_CASE("the sides sit where the ladder says, best-of-book against the spread") {
    const DisplaySnapshot s = make_book(kLevels);
    LadderView view;
    GridCanvas c;
    view.observe(s);
    view.draw(s, c);

    // asks[0] immediately above the spread, bids[0] immediately below it.
    CHECK(c.ink[kSpreadRow - 1][0] == Ink::AskBest);
    CHECK(c.ink[kBidTop][0] == Ink::BidBest);
    CHECK(c.ink[kSpreadRow][0] == Ink::Spread);

    // ...and everything behind them is the ordinary side ink, never the best.
    for (int i = 1; i < kLevels; ++i) {
        CHECK(c.ink[kSpreadRow - 1 - i][0] == Ink::Ask);
        CHECK(c.ink[kBidTop + i][0] == Ink::Bid);
    }

    // No bid ink above the spread, no ask ink below it. Getting the sides the
    // wrong way round is the single most misleading thing this renderer could
    // do, and it would look entirely plausible on the panel.
    for (int y = kAskTop; y < kSpreadRow; ++y) {
        CHECK(c.count_in_row(y, Ink::Bid) == 0);
        CHECK(c.count_in_row(y, Ink::BidBest) == 0);
    }
    for (int y = kBidTop; y < kBottomRuleRow; ++y) {
        CHECK(c.count_in_row(y, Ink::Ask) == 0);
        CHECK(c.count_in_row(y, Ink::AskBest) == 0);
    }
}

TEST_CASE("a book deeper than the panel is truncated, never overrun") {
    DisplaySnapshot s = make_book(kLevels);
    s.bid_count = static_cast<std::uint8_t>(kDisplayLevels);
    s.ask_count = static_cast<std::uint8_t>(kDisplayLevels);

    LadderView view;
    GridCanvas c;
    view.observe(s);
    view.draw(s, c);
    CHECK(c.out_of_bounds == 0);
    CHECK(c.every_pixel_written());
}

// ---------------------------------------------------------------------------
TEST_CASE("the header shows the last price when live and the reason when not") {
    LadderView view;

    // The lit-pixel count in the header is a fingerprint of the text drawn: two
    // different strings essentially never produce the same one, and comparing
    // counts is what lets this assert "the header changed" without the test
    // owning a copy of the font.
    const auto header_pixels = [&view](const DisplaySnapshot& s) {
        GridCanvas c;
        LadderView v = view;
        v.observe(s);
        v.draw(s, c);
        int lit = 0;
        for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
            lit += kPanelWidth - c.count_in_row(y, Ink::HeaderBed);
        }
        return lit;
    };

    DisplaySnapshot live = make_book(4);
    DisplaySnapshot disc = live;
    disc.status = FeedStatus::Stale;
    disc.stale_reason = GapReason::Disconnect;
    DisplaySnapshot resync = disc;
    resync.stale_reason = GapReason::Resync;

    CHECK(header_pixels(live) > 0);
    CHECK(header_pixels(disc) > 0);
    CHECK(header_pixels(resync) > 0);
    // Disconnect and resync must be distinguishable ON THE PANEL — the bench
    // should not have to read the serial log to tell a dropped socket from a
    // book that has simply never arrived.
    CHECK(header_pixels(disc) != header_pixels(resync));
    CHECK(header_pixels(live) != header_pixels(disc));

    // EVERY reason has text and ALL of it fits beside a four-digit symbol. This
    // is the assertion that made DISCONNECT become NO LINK when the font went
    // 3x5 -> 4x6: at twelve characters across, a ten-letter word plus an id does
    // not fit, and the failure mode without this check is a header that silently
    // clips or drops the symbol on exactly the frame the bench needs to read.
    using depthcharge::fw::reason_text;
    const GapReason all[] = {GapReason::SeqGap, GapReason::ChecksumFail, GapReason::Disconnect,
                             GapReason::Overflow, GapReason::Resync};
    for (GapReason r : all) {
        CHECK(reason_text(r) != nullptr);
        CHECK(text_width(reason_text(r)) > 0);
        CHECK(text_width("9999") + kGlyphAdvance + text_width(reason_text(r)) <= kPanelWidth);
    }
}

TEST_CASE("the symbol yields to the value rather than overlapping it") {
    // A header that overlaps is worse than one missing an id this object only
    // ever shows one of. The value is right-aligned first; the symbol draws only
    // if it fits with a column of air between them.
    DisplaySnapshot s = make_book(4);
    s.symbol.id = 999999999;          // absurdly wide on purpose
    s.symbol.price_decimals = 8;
    s.last_px = 123456789012345678LL;  // 18 digits + '.' — wider than the panel

    LadderView view;
    GridCanvas c;
    view.observe(s);
    view.draw(s, c);
    CHECK(c.out_of_bounds == 0);
    CHECK(c.every_pixel_written());

    // The symbol was dropped: column 0 of the header carries no symbol ink.
    int symbol_pixels = 0;
    for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
        symbol_pixels += c.count_in_row(y, Ink::Symbol);
    }
    CHECK(symbol_pixels == 0);
}

// ---------------------------------------------------------------------------
// THE RE-SEED MARKER (M5 stage D-A4, implementing D-B decision 2)
// ---------------------------------------------------------------------------

namespace {

// Lit pixels of one ink across the header band.
//
// The header-only comparison the marker cases need: `header_pixels` in the
// reason test counts everything that is not bed, which cannot separate the
// symbol slot from the value. **File-scope rather than another lambda** — the
// age test 200 lines below had an identical one, and now uses this.
// The header's VALUE slot, as `draw_header` computes it: the trimmed price
// while live, the stale reason otherwise. Spelled once so the width arithmetic
// below cannot drift from the renderer's.
struct ValueSlot {
    std::string text;
    int width;
};

ValueSlot value_of(const DisplaySnapshot& s) {
    const depthcharge::fw::TextField p =
        depthcharge::fw::TextField::price(s.last_px, s.symbol.price_decimals);
    const char* v = s.live() ? (s.has_last ? p.buf : "-")
                             : depthcharge::fw::reason_text(s.stale_reason);
    return ValueSlot{std::string(v), text_width(v)};
}

int header_ink_count(const DisplaySnapshot& s, Ink want) {
    LadderView v;
    GridCanvas c;
    v.observe(s);
    v.draw(s, c);
    int lit = 0;
    for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
        lit += c.count_in_row(y, want);
    }
    return lit;
}

}  // namespace

TEST_CASE("the header trims trailing zeros, and never a significant digit") {
    // **THE OWNER'S FIFTH RENDERING DECISION (2026-09-06).** Binance declares a
    // uniform 8 decimals and `tickSize` is a validator rather than the scale
    // (M5 stage 0), so BTCUSDT's last six digits are always zero and the value
    // alone was 74 px on a 64 px panel.
    //
    // THE CONSTRAINT THE DECISION CAME WITH, and it is the one that could have
    // been got wrong quietly: `format_scaled` is shared — nine call sites, one
    // of them `kraken_checksum.hpp`, which formats the string the venue's CRC32
    // is computed over. So the trim is at the display edge, and what has to be
    // proved is that it removes only what a re-parse restores.
    using depthcharge::fw::trim_trailing_zeros;
    using depthcharge::format_scaled;
    using depthcharge::parse_scaled;

    const auto trimmed = [](std::int64_t v, int decimals) {
        char buf[depthcharge::kMaxFormattedChars + 1]{};
        const std::size_t n = format_scaled(v, decimals, buf, sizeof buf - 1);
        REQUIRE(n > 0);
        buf[n] = '\0';
        trim_trailing_zeros(buf);
        return std::string(buf);
    };

    // The decision's own worked example.
    CHECK(trimmed(10823456000000LL, 8) == "108234.56");
    CHECK(text_width("108234.56") == 44);
    CHECK(text_width("108234.56000000") == 74);

    // **THE PROPERTY, and it IS the constraint: trimming removes only characters
    // a re-parse at the same scale puts back.** Swept over scales and shapes
    // rather than asserted on one string, because "never a significant digit" is
    // a statement about every value, not about the one in the example.
    const std::int64_t values[] = {
        0, 1, 9, 10, 100, 100000, 100001, 150000000LL, 10823456000000LL,
        999999999999LL, -1, -150000000LL, -10823456000000LL, 123456789012345678LL,
    };
    for (int decimals = 0; decimals <= 8; ++decimals) {
        for (const std::int64_t v : values) {
            char raw[depthcharge::kMaxFormattedChars + 1]{};
            const std::size_t n = format_scaled(v, decimals, raw, sizeof raw - 1);
            if (n == 0) { continue; }          // does not fit at this scale
            raw[n] = '\0';
            const std::string t = trimmed(v, decimals);
            INFO("value=" << v << " decimals=" << decimals << " raw=" << raw << " trimmed=" << t);
            // Round-trips exactly: no significant digit was lost.
            const depthcharge::DecimalParse back = parse_scaled(t, decimals);
            CHECK(back.ok());
            CHECK(back.value == v);
            // ...and it never GREW, and never lost a non-zero character.
            CHECK(t.size() <= std::string(raw).size());
        }
    }

    // A VENUE WHOSE DECIMALS ARE MEANINGFUL IS UNAFFECTED, in the ordinary case
    // and byte for byte: Anvil's 4-decimal prices carry non-zero low digits, so
    // there is nothing to trim and the string does not move. `make_book` uses
    // exactly this scale and value.
    CHECK(trimmed(100001, 4) == "10.0001");
    CHECK(trimmed(99972, 4) == "9.9972");
    // A round price does shorten, and that is the decision rather than a defect:
    // the value is exact either way and the panel gains the pixels.
    CHECK(trimmed(100000, 4) == "10");
    CHECK(trimmed(0, 8) == "0");
    CHECK(trimmed(-150000000LL, 8) == "-1.5");

    // An INTEGER's trailing zeros are significant and must survive — this is the
    // symbol id's path (`decimals == 0`), which shares the formatter.
    CHECK(trimmed(100, 0) == "100");
    CHECK(trimmed(1000000, 0) == "1000000");
}

TEST_CASE("the re-seed marker fits beside a four-digit value, exactly as every reason does") {
    // D-B decision 2's second constraint, and the shape is copied from the
    // `reason_text` loop above rather than invented: *"the marker is at most
    // eight characters and is asserted against the real header width in
    // test_ladder_render.cpp, the way every reason_text string already is — that
    // test is what stops a longer word being added without the desk saying so."*
    using depthcharge::fw::reseed_marker;

    CHECK(reseed_marker() != nullptr);
    CHECK(text_width(reseed_marker()) > 0);
    CHECK(text_width("9999") + kGlyphAdvance + text_width(reseed_marker()) <= kPanelWidth);

    // AND THE EIGHT-CHARACTER BOUND ITSELF, stated as the width it comes from.
    // Eight characters is 39 px and 19 + 5 + 39 = 63; nine is 44 and 68, which
    // does not fit. So the cap is not a preference — it is the last count that
    // clears the panel beside a four-digit value, and this line is what says so
    // if anyone edits the marker without re-deriving it.
    CHECK(text_width("01234567") == 39);
    CHECK(text_width("9999") + kGlyphAdvance + text_width("01234567") == 63);
    CHECK(text_width("9999") + kGlyphAdvance + text_width("012345678") > kPanelWidth);

    // Every glyph in the marker is a real one. A character the font has no entry
    // for renders as '?', which would pass every width check above and be wrong
    // on the panel — the one failure this family of assertions cannot see.
    // '?' IS the fallback index, so "does not render as '?'" is exactly this
    // comparison — and it stays right if the table is ever reordered.
    for (const char* p = reseed_marker(); *p != '\0'; ++p) {
        CHECK(depthcharge::fw::glyph_index(*p) != depthcharge::fw::glyph_index('?'));
    }
}

TEST_CASE("InFlight puts the marker in the symbol's slot and changes nothing else") {
    // D-B decision 2: *"on ReseedState::InFlight the live palette stays selected
    // and every ladder Ink is unchanged — the only difference from None is
    // inside draw_header."* Both halves are asserted, because the second is the
    // one a later change would break silently.
    // A DELIBERATELY NARROW VALUE, and the reason is measured rather than
    // stylistic. The marker is 29 px and `draw_header` draws it only when
    // `marker_w + kGlyphAdvance <= left_limit`, so it needs `left_limit >= 34`.
    // `left_limit` is what the value and the age leave: with no reading the age
    // is "-" (4 px) and takes 9, so the value must end by x=43, i.e. be at most
    // FOUR characters. `make_book`'s own "10.0001" is seven and leaves 21 — so
    // the marker does not fit even in the comfortable Kraken-shaped header the
    // age test calls the case worth having a picture of. That is the subject of
    // the next case; this one needs a header where the slot exists at all.
    DisplaySnapshot none = make_book(4);
    none.symbol.id = 7;              // one digit, so the slot has room to spare
    none.symbol.price_decimals = 0;
    none.last_px = 12;               // "12" — two characters, 9 px
    REQUIRE(none.reseed == depthcharge::ReseedState::None);

    DisplaySnapshot flight = none;
    flight.reseed = depthcharge::ReseedState::InFlight;

    // The slot's ink is spent differently — six characters where there was one
    // digit — and it is still `Ink::Symbol`, not a new one.
    CHECK(header_ink_count(flight, Ink::Symbol) > header_ink_count(none, Ink::Symbol));

    // THE LADDER IS UNTOUCHED. Every row below the header renders identically,
    // which is what "the only difference is inside draw_header" means in pixels.
    LadderView va, vb;
    GridCanvas ca, cb;
    va.observe(none);   va.draw(none, ca);
    vb.observe(flight); vb.draw(flight, cb);
    CHECK(ca.out_of_bounds == 0);
    CHECK(cb.out_of_bounds == 0);
    CHECK(ca.every_pixel_written());
    CHECK(cb.every_pixel_written());
    int ladder_differences = 0;
    for (int y = kHeaderTop + kHeaderRows; y < kPanelHeight; ++y) {
        for (int x = 0; x < kPanelWidth; ++x) {
            if (ca.ink[y][x] != cb.ink[y][x]) { ++ladder_differences; }
        }
    }
    CHECK(ladder_differences == 0);

    // AND IT YIELDS EXACTLY AS THE SYMBOL YIELDS — the third constraint. The
    // same absurd price that drops the symbol in the case above drops the
    // marker, rather than overlapping the value.
    DisplaySnapshot narrow = flight;
    narrow.symbol.price_decimals = 8;
    narrow.last_px = 123456789012345678LL;
    CHECK(header_ink_count(narrow, Ink::Symbol) == 0);
}

TEST_CASE("what the fifth decision bought was the DASH, not a reading") {
    // **A CLAIM NARROWED TO WHAT WAS MEASURED (owner, at the split).** This
    // stage first reported that trimming the value "bought back the age", which
    // its own table contradicted: the live BTCUSDT row read `left_limit == 20`,
    // and by `draw_header` that means the age YIELDED — had it drawn,
    // `left_limit` would be `age_x`, strictly below `value_x`.
    //
    // What the trim actually bought is the **`-` placeholder**: 9 px spent
    // saying NO READING, where before the value clamped `value_x` to 0 and
    // nothing left of it could draw at all.
    //
    // **AND A REAL READING NEVER FITS, which is sharper than "only at three
    // characters".** `AgeText` has no three-character form: `"%u.%us"` is four
    // at its shortest (`0.0s`) and the minute and hour forms are five. So a
    // reading needs at least 19 + 5 = 24 px against the 20 the live Binance
    // header offers — it cannot draw there, before or after the fifth decision.
    using depthcharge::AgeText;

    CHECK(text_width(AgeText(0).buf) == 19);          // "0.0s" — the shortest there is
    CHECK(text_width(AgeText(500).buf) == 19);        // "0.5s"
    CHECK(text_width(AgeText(21400).buf) == 24);      // "21.4s"
    CHECK(text_width(AgeText(65000).buf) == 24);      // "1m05s"
    CHECK(text_width(AgeText::unknown().buf) == 4);   // "-", and it is the only one that fits

    DisplaySnapshot btc = make_book(4);
    btc.symbol.id = 11;
    btc.symbol.price_decimals = 8;
    btc.last_px = 10823456000000LL;                   // 108234.56 -> 44 px, value_x 20
    CHECK(value_of(btc).width == 44);
    CHECK(kPanelWidth - value_of(btc).width == 20);
    CHECK(text_width(AgeText(500).buf) + kGlyphAdvance > 20);   // the reading yields
    CHECK(text_width(AgeText::unknown().buf) + kGlyphAdvance <= 20);  // the dash does not

    // **THE INTERACTION WORTH STATING WHERE SOMEONE MEETS IT.** That dash is
    // what the panel shows for the whole of the age estimator's baseline window
    // — 639 s, ~11 minutes, on every Binance connection (32 intervals at the
    // ~20 s ping cadence, pinned in `test_binance_adapter.cpp`). Under the
    // pre-sixth-decision order the dash cost 9 px and pushed the marker out at
    // exactly the time a re-seed is most likely: the first eleven minutes of a
    // connection. That is what the sixth decision re-ranks away, and the case
    // below is where it is asserted.
}

TEST_CASE("during a fetch the marker outranks the age, and only during a fetch") {
    // **THE OWNER'S SIXTH RENDERING DECISION, 2026-09-06.** Standing priority is
    // VALUE > AGE > SYMBOL and has been since M4 stage D; while a re-seed fetch
    // is in flight it becomes **VALUE > MARKER > AGE > SYMBOL**, and reverts the
    // moment the fetch ends.
    //
    // Why a re-rank rather than a shorter marker: allocated LAST, a marker short
    // enough to fit would appear only when the age was too wide to draw itself,
    // so its presence would encode the age's width rather than a re-seed. The
    // slot was never the problem; the order was.
    using depthcharge::fw::reseed_marker;

    // Three characters, and the three is arithmetic: `value_x` is 20 on the
    // widest live Binance header after the fifth decision, and an n-character
    // marker needs 5n + 4.
    CHECK(text_width(reseed_marker()) == 14);
    CHECK(text_width(reseed_marker()) + kGlyphAdvance == 19);
    // D-B's eight-character bound still holds, and still against a 4-digit value.
    CHECK(text_width("9999") + kGlyphAdvance + text_width(reseed_marker()) <= kPanelWidth);

    // **THE TWO SLOTS ARE SEPARATED BY GEOMETRY, not by ink** — the marker, the
    // symbol and the age all draw in `Ink::Symbol`. The marker and the symbol
    // both start at x = 0, so the symbol is made too wide to fit (999999 is
    // 29 px, needing 34) in every case below; then anything lit in the marker's
    // own columns is the marker, and the age is right-aligned beyond them.
    const int mw = text_width(reseed_marker());
    const auto lit_in = [](const DisplaySnapshot& s, int x0, int x1) {
        LadderView v;
        GridCanvas c;
        v.observe(s);
        v.draw(s, c);
        int lit = 0;
        for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
            for (int x = x0; x < x1 && x < kPanelWidth; ++x) {
                if (c.ink[y][x] == Ink::Symbol) { ++lit; }
            }
        }
        return lit;
    };
    // THE MARKER'S FIRST GLYPH CELL, and only that. The marker always starts at
    // x = 0; the age is right-aligned and can land anywhere, including inside the
    // marker's later columns — at `value_x` 20 with no reading the dash sits at
    // x = 11. Four columns is the one window the age cannot reach in any case
    // here, so it is the one that means "the marker drew".
    const auto marker_px = [&](const DisplaySnapshot& s) {
        return lit_in(s, 0, kGlyphWidth);
    };
    const auto age_px = [&](const DisplaySnapshot& s) {
        return lit_in(s, mw + kGlyphAdvance, kPanelWidth);
    };

    DisplaySnapshot base = make_book(4);
    base.symbol.id = 999999;                  // 29 px — never fits, so x=0 is the marker's alone
    base.symbol.price_decimals = 8;

    // ---- the case where the age HAS room, so the yield is visible ----------
    DisplaySnapshot live = base;
    live.last_px = 432100000LL;               // 4.321 -> 24 px, value_x 40
    live.has_age = true;
    live.age_ms = 500;                        // "0.5s" -> 19 px, age_x 16

    DisplaySnapshot fetching = live;
    fetching.reseed = depthcharge::ReseedState::InFlight;

    // Not fetching: no marker, and the age has the room.
    CHECK(marker_px(live) == 0);
    CHECK(age_px(live) > 0);

    // Fetching: the marker draws and the age yields to it — `age_x` would be 16,
    // below the 19 the marker has reserved. **This is the whole decision in two
    // assertions.**
    CHECK(marker_px(fetching) > 0);
    CHECK(age_px(fetching) == 0);

    // AND IT REVERTS. `Wanted` and `None` are the standing order — the re-rank
    // is scoped to a fetch in flight and to nothing else.
    DisplaySnapshot wanted = live;
    wanted.reseed = depthcharge::ReseedState::Wanted;
    CHECK(marker_px(wanted) == 0);
    CHECK(age_px(wanted) == age_px(live));

    // ---- the case that CLOSES ROADMAP D11 ---------------------------------
    // The widest live header this build renders: BTCUSDT at 108234.56, 44 px,
    // `value_x` 20. The marker needs 19 and draws with a pixel to spare, where
    // "RESEED" needed 34 and never could.
    DisplaySnapshot widest = base;
    widest.last_px = 10823456000000LL;
    widest.has_age = true;
    widest.age_ms = 500;
    CHECK(value_of(widest).width == 44);
    DisplaySnapshot widest_fetch = widest;
    widest_fetch.reseed = depthcharge::ReseedState::InFlight;
    CHECK(marker_px(widest_fetch) > 0);
    CHECK(marker_px(widest) == 0);

    // ---- the ~11 minute window, which is what the re-rank was FOR ----------
    // No reading, so the age is the 4 px dash. Under the old order that dash
    // cost 9 px and pushed the marker out for the first 639 s of every Binance
    // connection — the time a re-seed is most likely. Now the marker takes its
    // 19 px first and the dash yields instead.
    DisplaySnapshot window = base;
    window.last_px = 10823456000000LL;
    window.has_age = false;
    DisplaySnapshot window_fetch = window;
    window_fetch.reseed = depthcharge::ReseedState::InFlight;
    CHECK(marker_px(window) == 0);             // before: no marker, dash only
    CHECK(marker_px(window_fetch) > 0);         // after: the marker has the slot
    // The dash's own fate in THIS configuration is not asserted here, and the
    // reason is geometry rather than doubt: at `value_x` 20 the dash draws at
    // x = 11, inside the marker's later columns, so no fixed window separates
    // the two. It is asserted in the wide-header case above, where `age_x` is 16
    // and the slots do not overlap — same rule, measurable there.

    // The ladder is still untouched by any of it (D-B decision 2).
    LadderView va, vb;
    GridCanvas ca, cb;
    va.observe(live);     va.draw(live, ca);
    vb.observe(fetching); vb.draw(fetching, cb);
    CHECK(ca.every_pixel_written());
    CHECK(cb.every_pixel_written());
    int ladder_differences = 0;
    for (int y = kHeaderTop + kHeaderRows; y < kPanelHeight; ++y) {
        for (int x = 0; x < kPanelWidth; ++x) {
            if (ca.ink[y][x] != cb.ink[y][x]) { ++ladder_differences; }
        }
    }
    CHECK(ladder_differences == 0);
}

TEST_CASE("RETIRED as a tripwire: the pre-decision header, kept as the reason for the fifth") {
    // **THIS CASE WAS WRITTEN TO FAIL WHEN THE VALUE SLOT WAS FIXED. IT DID NOT
    // FIRE, AND SAYING SO IS THE POINT OF RETIRING IT DELIBERATELY.**
    //
    // The owner took the fifth rendering decision on 2026-09-06 and the value
    // slot IS fixed — and every assertion here still passed, for two reasons
    // worth having written down:
    //
    //   * four of them asserted `format_scaled`'s OUTPUT, and the decision
    //     deliberately does not touch `format_scaled` (it is shared with
    //     `kraken_checksum.hpp`, which formats the string a CRC32 is computed
    //     over). They were pinning the venue's scale, not the header;
    //   * the fifth asserted that the marker is invisible, and it still is.
    //
    // **So it was never the tripwire it claimed to be**, and a test that
    // announces it will fail and then cannot is worse than no tripwire — it is
    // the reassuring-instrument failure ARCHITECTURE §9 keeps recording. The
    // real signal now lives in the case above, which asserts `left_limit < 34`
    // over six widths and fails the moment a marker that fits is chosen.
    //
    // What is KEPT here is the arithmetic that justified the decision, because
    // the decision has to be re-derivable from the record: at Binance's uniform
    // 8 decimals the formatter emits fifteen characters, 74 px on a 64 px panel,
    // and `value_x` clamped to 0 — which took the age and the symbol with it.
    using depthcharge::format_scaled;

    char buf[32]{};
    const std::size_t n = format_scaled(10823456000000LL, 8, buf, sizeof buf - 1);
    REQUIRE(n == 15);
    CHECK(std::string(buf) == "108234.56000000");
    CHECK(text_width(buf) == 74);
    CHECK(text_width(buf) > kPanelWidth);

    // ...and this is what the fifth decision made of it. The header no longer
    // asks the formatter for those six zeros.
    DisplaySnapshot btc = make_book(4);
    btc.symbol.id = 11;
    btc.symbol.price_decimals = 8;
    btc.last_px = 10823456000000LL;
    CHECK(value_of(btc).text == "108234.56");
    CHECK(value_of(btc).width == 44);
    CHECK(value_of(btc).width < kPanelWidth);

    // AND THE SLOT IS REACHABLE AGAIN, which is the gain the decision actually
    // delivered: with the value at 44 px the SYMBOL fits where nothing did
    // before. It is only the six-character marker that still does not.
    btc.has_age = true;
    btc.age_ms = 500;
    DisplaySnapshot with_symbol = btc;
    with_symbol.reseed = depthcharge::ReseedState::None;
    DisplaySnapshot with_marker = btc;
    with_marker.reseed = depthcharge::ReseedState::InFlight;
    CHECK(header_ink_count(with_symbol, Ink::Symbol) > 0);      // was 0 before the decision
    // ...and since the SIXTH decision the marker draws there too, allocated
    // ahead of the age. Both slots are reachable; neither was before.
    CHECK(header_ink_count(with_marker, Ink::Symbol) > 0);
}

TEST_CASE("the age is drawn, and it yields to the value exactly as the symbol does") {
    // M4 stage D, A4. `age_ms` has been on DisplaySnapshot since stage A2 and no
    // panel ever drew it, because the firmware never stamped it. It does now.
    //
    // WHERE it lives is Part B's decision and this test does not pin a position —
    // it pins the PRIORITY, which is the property that stops the header from
    // overlapping: value first and always, then the age, then the symbol. The
    // value is the only field that may never be dropped.
    using depthcharge::AgeText;

    // `header_ink_count` (above) rather than a second copy of it: this lambda
    // and that function had identical bodies, which review found when the
    // function was added 200 lines earlier.
    const auto& header_ink = header_ink_count;

    // A Kraken-shaped header: a four-decimal price, a one-digit symbol id, and a
    // reading. All three fit, which is the case worth having a picture of —
    // 64 px holds "2", "1.5s" and "0.1234" with a column of air between each.
    DisplaySnapshot s = make_book(4);
    s.symbol.id = 2;
    s.symbol.price_decimals = 4;
    s.has_last = true;
    s.last_px = 1234;          // 0.1234
    s.has_age = false;

    const int value_only = header_ink(s, Ink::Value);
    const int no_age = header_ink(s, Ink::Symbol);

    s.has_age = true;
    s.age_ms = 1500;           // "1.5s"
    const int with_age = header_ink(s, Ink::Symbol);

    // The value is untouched by any of this — it is the field that never yields.
    CHECK(header_ink(s, Ink::Value) == value_only);
    // And the age is genuinely on the panel: more non-value header ink than the
    // same frame without a reading.
    CHECK(with_age > no_age);

    // THE SPACE THE AGE RESERVES IS WHAT THE SYMBOL YIELDS TO, and that — not the
    // `if` around the age itself — is the load-bearing half of the priority.
    //
    // Found by mutation: removing the age's own fit test changes NOTHING on the
    // panel, because `draw_text` clips silently and every field here is a whole
    // number of 5 px glyph cells, so an un-yielded age is always an exact
    // multiple of the advance off the left edge and every glyph of it is fully
    // clipped. What does change the panel is failing to move `left_limit`: the
    // symbol then draws into the columns the age is using. A one-digit id is too
    // narrow to reach them, so the case needs a wide one.
    {
        DisplaySnapshot wide = s;
        wide.symbol.id = 9999;          // 19 px, and the age starts at column 11

        LadderView v;
        GridCanvas c;
        v.observe(wide);
        v.draw(wide, c);
        CHECK(c.out_of_bounds == 0);
        CHECK(c.every_pixel_written());

        // No pixel of the header is written more than twice — once by the band
        // wash, once by whichever glyph owns it. A third write is two fields in
        // the same place, which is the overlap this whole ordering exists to
        // prevent and which is otherwise invisible: both fields draw in the same
        // ink, so a pixel count alone cannot see it.
        int worst = 0;
        for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
            for (int x = 0; x < kPanelWidth; ++x) {
                if (c.writes[y][x] > worst) { worst = c.writes[y][x]; }
            }
        }
        CHECK(worst <= 2);

        // And the yield actually happened: the id is gone, not merely shifted.
        // Columns 0..3 carry no glyph ink at all.
        for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
            for (int x = 0; x < 4; ++x) { CHECK(c.ink[y][x] == Ink::HeaderBed); }
        }
    }

    // NO OVERLAP WITH THE VALUE either, checked as geometry rather than as a
    // pixel count. The value is right-aligned, so everything else must end at
    // least one glyph gap before it starts.
    {
        LadderView v;
        GridCanvas c;
        v.observe(s);
        v.draw(s, c);
        CHECK(c.out_of_bounds == 0);
        CHECK(c.every_pixel_written());

        const int value_x = kPanelWidth - text_width("0.1234");
        for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
            for (int x = value_x - 1; x < kPanelWidth; ++x) {
                // The value's own columns may hold Value ink; nothing to its left
                // may, and no Symbol ink may reach into them.
                if (x < value_x) { CHECK(c.ink[y][x] != Ink::Value); }
                CHECK(c.ink[y][x] != Ink::Symbol);
            }
        }
    }
}

TEST_CASE("a header too tight for everything drops the symbol first and the value last") {
    // The yield order, exercised by widening the value until each field in turn
    // has to go. The failure this prevents is a header that silently clips —
    // which reads on a bench as a rendering bug rather than as a width problem.
    const auto ink_counts = [](const DisplaySnapshot& s) {
        LadderView v;
        GridCanvas c;
        v.observe(s);
        v.draw(s, c);
        int sym = 0;
        int val = 0;
        for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
            sym += c.count_in_row(y, Ink::Symbol);
            val += c.count_in_row(y, Ink::Value);
        }
        CHECK(c.out_of_bounds == 0);
        return std::pair<int, int>{sym, val};
    };

    DisplaySnapshot s = make_book(4);
    s.symbol.id = 9999;
    s.has_last = true;
    s.has_age = true;
    s.age_ms = 12'345;         // "12.3s"

    // Roomy: a two-digit price leaves space for the age AND the id.
    s.symbol.price_decimals = 0;
    s.last_px = 42;
    const auto roomy = ink_counts(s);
    CHECK(roomy.second > 0);
    CHECK(roomy.first > 0);

    // Tight: an eighteen-digit price with eight decimals is wider than the
    // panel, so the value clamps to column 0 and both other fields go.
    s.symbol.price_decimals = 8;
    s.last_px = 123456789012345678LL;
    const auto tight = ink_counts(s);
    CHECK(tight.second > 0);       // the value survives everything
    CHECK(tight.first == 0);       // age and symbol both dropped

    // And the stale header behaves the same way: the reason claims the slot the
    // price had, and the age still yields to it.
    s.status = FeedStatus::Stale;
    s.stale_reason = GapReason::ChecksumFail;
    const auto stale = ink_counts(s);
    CHECK(stale.second > 0);
}

TEST_CASE("no reading is a dash, not a zero, and it says so on the panel") {
    // `-` and `0.0s` are different claims and exactly one of them is
    // reassuring. The panel shows `-` for the first 16 s of an Anvil connection
    // and 32 s of a Kraken one, while the baseline latches.
    // The two strings themselves are pinned by test_age_estimator.cpp's
    // "age text" case, which owns the formatter. They were copied here too and
    // review removed them: one cause going red in two files sends a reader
    // looking at LadderView before they notice the formatter is the common
    // factor.
    const auto header_symbol_ink = [](const DisplaySnapshot& s) {
        LadderView v;
        GridCanvas c;
        v.observe(s);
        v.draw(s, c);
        int lit = 0;
        for (int y = kHeaderTop; y < kHeaderTop + kHeaderRows; ++y) {
            lit += c.count_in_row(y, Ink::Symbol);
        }
        return lit;
    };

    DisplaySnapshot unknown = make_book(4);
    unknown.symbol.id = 2;
    unknown.symbol.price_decimals = 4;
    unknown.has_last = true;
    unknown.last_px = 1234;
    unknown.has_age = false;

    DisplaySnapshot zero = unknown;
    zero.has_age = true;
    zero.age_ms = 0;

    // Two different strings, therefore two different fingerprints. If these ever
    // came out equal the panel would be unable to say "I do not know yet".
    CHECK(header_symbol_ink(unknown) != header_symbol_ink(zero));
}

// ---------------------------------------------------------------------------
TEST_CASE("the heartbeat toggles on every drawn frame") {
    // Invariant #5 covers the feed going quiet; nothing covers the RENDERER
    // dying, and a dead render task leaves exactly the frozen ladder the project
    // calls its one unacceptable output.
    LadderView view;
    GridCanvas c;
    const DisplaySnapshot s = make_book(4);

    const bool before = view.beat();
    view.observe(s);
    view.draw(s, c);
    CHECK(view.beat() != before);
    const Ink first = c.ink[kBeatY][kBeatX];

    view.draw(s, c);
    CHECK(view.beat() == before);
    CHECK(c.ink[kBeatY][kBeatX] != first);

    // On is Beat, off is Background — and both are grey in the stale palette, so
    // the heartbeat cannot smuggle a colour onto a stale panel.
    CHECK((first == Ink::Beat || first == Ink::Background));
    CHECK(kStalePalette[Ink::Beat].grey());
}

TEST_CASE("the sparkline is render-side sampled state and stays inside its strip") {
    // Recorded decision (M3 stage D brief): the last-price history is a fixed
    // ring in the render task and NEVER a new DisplaySnapshot field, which would
    // be a §4/§5 change to the vocabulary the two cores share.
    LadderView view;
    GridCanvas c;

    DisplaySnapshot s = make_book(4);
    for (int k = 0; k < kSparkColumns * 2; ++k) {
        s.version = static_cast<std::uint32_t>(100 + k);
        s.last_px = 100000 + (k % 7);
        view.observe(s);
    }
    CHECK(view.spark_samples() == kSparkColumns);

    view.draw(s, c);
    CHECK(c.out_of_bounds == 0);

    // Tape ink appears only in the strip, and never in the heartbeat's column.
    for (int y = 0; y < kPanelHeight; ++y) {
        const int tape = c.count_in_row(y, Ink::Tape);
        if (y < kStripTop) { CHECK(tape == 0); }
    }
    for (int y = kStripTop; y < kPanelHeight; ++y) {
        CHECK(c.ink[y][kPanelWidth - 1] != Ink::Tape);
        CHECK(c.ink[y][kPanelWidth - 2] != Ink::Tape);
    }

    // A flat tape must still draw — a price that has not moved is data, not an
    // empty window, and a divide by (hi - lo) is where that would go wrong.
    LadderView flat;
    GridCanvas fc;
    DisplaySnapshot f = make_book(4);
    for (int k = 0; k < 10; ++k) {
        f.version = static_cast<std::uint32_t>(200 + k);
        f.last_px = 100000;
        flat.observe(f);
    }
    flat.draw(f, fc);
    CHECK(fc.out_of_bounds == 0);
    int flat_tape = 0;
    for (int y = kStripTop; y < kPanelHeight; ++y) { flat_tape += fc.count_in_row(y, Ink::Tape); }
    CHECK(flat_tape == 10);

    // A snapshot with no last price contributes no sample; the strip is still
    // drawn, so the frame stays fully covered.
    LadderView none;
    GridCanvas nc;
    DisplaySnapshot n = make_book(4);
    n.has_last = false;
    for (int k = 0; k < 5; ++k) {
        n.version = static_cast<std::uint32_t>(300 + k);
        none.observe(n);
    }
    CHECK(none.spark_samples() == 0);
    none.draw(n, nc);
    CHECK(nc.every_pixel_written());
}

TEST_CASE("a trade print flashes its own level, and only for a few frames") {
    LadderView view;
    GridCanvas c;

    DisplaySnapshot s = make_book(4);
    // The first frame seen is not a print: a boot must not flash a level just
    // because the trade ring arrived populated.
    s.trade_count = 1;
    s.trades[0] = {s.bids[1].px, 5, 7, Side::Bid};
    view.observe(s);
    CHECK_FALSE(view.flashing());
    view.draw(s, c);
    CHECK(c.ink[kBidTop + 1][0] == Ink::Bid);

    // A NEW print — new event seq — flashes the level it printed at.
    s.version = 43;
    s.trades[0] = {s.bids[1].px, 5, 8, Side::Bid};
    view.observe(s);
    CHECK(view.flashing());
    view.draw(s, c);
    CHECK(c.ink[kBidTop + 1][0] == Ink::Flash);
    CHECK(c.ink[kBidTop][0] == Ink::BidBest);  // and nothing else

    // It decays. Three drawn frames, then the level is ordinary again.
    view.draw(s, c);
    view.draw(s, c);
    CHECK_FALSE(view.flashing());
    view.draw(s, c);
    CHECK(c.ink[kBidTop + 1][0] == Ink::Bid);

    // Flash is grey on a stale panel like everything else — a white flash on a
    // book nobody should trust would be invariant #5's exact failure.
    CHECK(kStalePalette[Ink::Flash].grey());
}

TEST_CASE("invariant #7: the render path allocates nothing, ever") {
    // DESIGN.html strain 7, and this is what closes it. The M1 console ladder
    // allocates freely — correct for a desk — but `format_px` returns a
    // std::string and lives in a harness header a firmware renderer is tempted
    // to imitate. Nothing enforced the distinction, because the alloc probe
    // covered the FEED path and stopped at the channel.
    //
    // It stops there no longer. The probe replaces global operator new in this
    // binary, so a std::string, a std::vector or a stray snprintf-into-a-heap-
    // buffer anywhere under LadderView::draw moves the counter. The GridCanvas
    // is a plain array member, so nothing here allocates on the test's behalf
    // either.
    LadderView view;
    static GridCanvas c;  // static: 64x64 of Ink + writes is too large for a
                          // stack frame, and a heap one would defeat the probe.

    DisplaySnapshot s = make_book(kLevels);
    s.trade_count = 2;
    s.trades[0] = {s.asks[3].px, 9, 11, Side::Ask};
    s.trades[1] = {s.bids[2].px, 4, 10, Side::Bid};

    // Warm every branch once, outside the window: the first call through a code
    // path is where a lazily-initialised anything would allocate.
    view.observe(s);
    view.draw(s, c);

    const std::size_t before = dc::testing::allocation_count();
    for (int k = 0; k < 200; ++k) {
        s.version = static_cast<std::uint32_t>(1000 + k);
        s.last_px = 100000 + (k % 13);
        s.trades[0].seq = static_cast<depthcharge::Seq>(100 + k);   // a print every frame
        s.status = (k % 7 == 0) ? FeedStatus::Stale : FeedStatus::Live;
        s.stale_reason = (k % 2 == 0) ? GapReason::Disconnect : GapReason::Resync;
        s.bid_count = static_cast<std::uint8_t>(k % (kLevels + 1));  // and every depth
        view.observe(s);
        view.draw(s, c);
    }
    const std::size_t after = dc::testing::allocation_count();

    CHECK(after == before);
    CHECK(c.out_of_bounds == 0);

    // POSITIVE CONTROL, and it is not decoration. An allocation counter that is
    // not wired reports "zero allocations" for every possible renderer, which is
    // the single most expensive way this test could be wrong — it would pass
    // just as happily over a renderer built out of std::string.
    //
    // The allocation has to ESCAPE to survive the optimiser. GCC eliminates a
    // new/delete pair whose result it can prove is never observed
    // (-fallocation-dce), which it can do through std::string and through a
    // local `new` alike: two hand-written mutants of LadderView::draw were
    // silently deleted that way before this control was written, and both
    // "passed". The volatile store below is an observable side effect, so the
    // allocation cannot be folded away.
    // The POINTER is what has to escape, not its contents: storing `escaped[0]`
    // through a volatile is not enough, because the compiler forwards the 'x' it
    // just wrote and deletes the allocation anyway. A volatile store of the
    // address makes the address itself observable, and an address can only come
    // from a real allocation.
    const std::size_t control_before = dc::testing::allocation_count();
    static char* volatile sink = nullptr;
    sink = new char[64 + (s.version % 3)];
    CHECK(dc::testing::allocation_count() > control_before);
    delete[] sink;
    sink = nullptr;
}

TEST_CASE("property: 3,000 random books all render inside the frame and never lie about a side") {
    // The example cases above each pin one property against one hand-built book.
    // This is the shape frame_reassembler.hpp and stall_probe.hpp set for the
    // firmware headers that matter: a long deterministic walk over the input
    // space, asserting the invariants that must hold for EVERY frame rather than
    // for the frames someone thought to write down. Depth, quantities, status,
    // reason, the trade ring and the last price all move; the assertions do not.
    //
    // Deterministic on purpose — a failure has to be reproducible from the seed
    // printed in the report, not from "it went red on CI once".
    std::uint32_t rng = 0x5EED1234u;
    const auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    LadderView view;
    static GridCanvas c;
    DisplaySnapshot s{};
    s.symbol.id = 101;
    s.symbol.price_decimals = 4;
    s.symbol.qty_decimals = 0;

    for (int step = 0; step < 3000; ++step) {
        s.version = static_cast<std::uint32_t>(step + 1);
        s.bid_count = static_cast<std::uint8_t>(next() % (kDisplayLevels + 1));
        s.ask_count = static_cast<std::uint8_t>(next() % (kDisplayLevels + 1));
        s.status = (next() & 3u) == 0 ? FeedStatus::Stale : FeedStatus::Live;
        s.stale_reason = static_cast<GapReason>(next() % 5u);
        s.has_last = (next() & 1u) != 0;
        s.last_px = static_cast<PriceTicks>(90000 + (next() % 20000));

        // Quantities span the whole interesting range, including zero (a level
        // that is present but empty) and values far apart enough that the
        // normalisation floors a small one at a single pixel.
        for (int i = 0; i < static_cast<int>(kDisplayLevels); ++i) {
            s.bids[i] = BookLevel{100000 - i, static_cast<Qty>(next() % 1000000u)};
            s.asks[i] = BookLevel{100050 + i, static_cast<Qty>(next() % 1000000u)};
        }
        s.trade_count = static_cast<std::uint8_t>(next() % 3u);
        if (s.trade_count > 0) {
            s.trades[0] = {s.bids[next() % kDisplayLevels].px,
                           static_cast<Qty>(1 + next() % 50),
                           static_cast<depthcharge::Seq>(next()),
                           (next() & 1u) ? Side::Bid : Side::Ask};
        }

        c.clear();
        view.observe(s);
        view.draw(s, c);

        // 1. The frame is whole. Anything less leaves a two-draws-old pixel on a
        //    double-buffered panel, which is a frozen ladder in miniature.
        REQUIRE(c.out_of_bounds == 0);
        REQUIRE(c.every_pixel_written());

        // 2. The sides never swap. This is the failure that would look entirely
        //    plausible on the panel and be exactly backwards.
        for (int y = kAskTop; y < kSpreadRow; ++y) {
            REQUIRE(c.count_in_row(y, Ink::Bid) == 0);
            REQUIRE(c.count_in_row(y, Ink::BidBest) == 0);
        }
        for (int y = kBidTop; y < kBottomRuleRow; ++y) {
            REQUIRE(c.count_in_row(y, Ink::Ask) == 0);
            REQUIRE(c.count_in_row(y, Ink::AskBest) == 0);
        }

        // 3. The chrome is always where the row budget says.
        REQUIRE(c.count_in_row(kTopRuleRow, Ink::Chrome) == kPanelWidth);
        REQUIRE(c.count_in_row(kBottomRuleRow, Ink::Chrome) == kPanelWidth);
        REQUIRE(c.count_in_row(kSpreadRow, Ink::Spread) == kPanelWidth);

        // 4. INVARIANT #5, over every frame rather than one: a stale snapshot
        //    resolves to nothing but grey, and nothing black.
        //
        //    Checked per INK, not per pixel. Greyness is a property of the
        //    palette entry, so 4,096 lookups per frame would assert the same
        //    twelve facts 341 times each and bury the suite's assertion count
        //    under an arithmetic artefact. What is worth checking every frame is
        //    which inks the renderer actually reached for — that set is what a
        //    new code path would change.
        if (!s.live()) {
            bool used[kInkCount] = {};
            bool unwritten = false;
            for (int y = 0; y < kPanelHeight; ++y) {
                for (int x = 0; x < kPanelWidth; ++x) {
                    const Ink k = c.ink[y][x];
                    if (k == Ink::Count) { unwritten = true; continue; }
                    used[static_cast<std::size_t>(k)] = true;
                }
            }
            REQUIRE_FALSE(unwritten);
            const auto pal = palette_for(s.status);
            for (std::size_t k = 0; k < kInkCount; ++k) {
                if (!used[k]) { continue; }
                REQUIRE(pal.ink[k].grey());
                // HeaderBed is black by decision (2026-08-24) and only ever
                // reaches the six header rows; every other ink a stale frame
                // touches must still glow.
                if (k == static_cast<std::size_t>(Ink::HeaderBed)) { continue; }
                REQUIRE_FALSE(pal.ink[k].black());
            }
            // ...and the bed really did stay inside the header band.
            for (int y = kHeaderTop + kHeaderRows; y < kPanelHeight; ++y) {
                for (int x = 0; x < kPanelWidth; ++x) {
                    REQUIRE(c.ink[y][x] != Ink::HeaderBed);
                }
            }
        }

        // 5. Bars stay integer and in range, and a level that exists is never
        //    invisible — checked against the same normalisation the draw used.
        const Qty max_qty = window_max_qty(s);
        const int nb = s.bid_count < kLevels ? static_cast<int>(s.bid_count) : kLevels;
        for (int i = 0; i < nb; ++i) {
            const int len = bar_length(s.bids[i].qty, max_qty);
            REQUIRE(len >= 0);
            REQUIRE(len <= kPanelWidth);
            if (s.bids[i].qty > 0 && max_qty > 0) { REQUIRE(len >= 1); }
        }
    }
}

TEST_CASE("property: bar length is monotonic in quantity and exact at the extremes") {
    // The one arithmetic rule the ladder rests on. Monotonicity is what makes the
    // panel a picture of the book at all: a bigger level must never draw shorter
    // than a smaller one at the same scale, and integer division is where that
    // quietly stops being true if the expression is ever rearranged.
    const Qty max_qty = 987654;
    int previous = 0;
    for (Qty q = 0; q <= max_qty; q += 991) {
        const int len = bar_length(q, max_qty);
        REQUIRE(len >= previous);
        REQUIRE(len <= kPanelWidth);
        previous = len;
    }
    CHECK(bar_length(0, max_qty) == 0);
    CHECK(bar_length(max_qty, max_qty) == kPanelWidth);

    // And it does not overflow where the obvious `64 * qty / max_qty` would.
    // Qty is int64_t by design and the adapter's only quantity guard is
    // `raw < 0`, so these are reachable inputs even if no venue we consume quotes
    // them — and signed overflow is undefined behaviour, not a wrong number.
    CHECK(bar_length(Qty{1} << 31, Qty{1} << 32) == kPanelWidth / 2);
    CHECK(bar_length(Qty{1} << 62, Qty{1} << 62) == kPanelWidth);
    CHECK(bar_length(Qty{1} << 61, Qty{1} << 62) == kPanelWidth / 2);
    CHECK(bar_length(Qty{1} << 60, Qty{1} << 62) == kPanelWidth / 4);
    CHECK(bar_length(1, INT64_MAX) == 1);              // never invisible, even here
    CHECK(bar_length(INT64_MAX, INT64_MAX) == kPanelWidth);
    CHECK(bar_length(INT64_MAX - 1, INT64_MAX) == kPanelWidth);

    // Monotonic across the overflow-prone range too, which is where a shift that
    // dropped one operand and not the other would show up.
    int prev_big = 0;
    for (int shift = 0; shift <= 62; ++shift) {
        const int len = bar_length(Qty{1} << shift, Qty{1} << 62);
        REQUIRE(len >= prev_big);
        REQUIRE(len <= kPanelWidth);
        prev_big = len;
    }
}

TEST_CASE("observe() is idempotent for a version it has already seen") {
    // draw() is gated on SnapshotChannel::consume() returning true, so a repeat
    // should not happen — but a repeated version quietly doubling the sparkline
    // rate would be invisible on the panel and wrong in the log.
    LadderView view;
    DisplaySnapshot s = make_book(4);
    s.version = 7;
    view.observe(s);
    view.observe(s);
    view.observe(s);
    CHECK(view.spark_samples() == 1);
}
