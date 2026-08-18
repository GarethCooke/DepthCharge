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
        CHECK_FALSE(c.black());
    }

    // Only the live background may be pure black; anything else black would be
    // an Ink somebody added and forgot to give a colour.
    for (std::size_t i = 0; i < kInkCount; ++i) {
        if (i == static_cast<std::size_t>(Ink::Background)) { continue; }
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
        header_lit += kPanelWidth - c.count_in_row(y, Ink::Background);
    }
    CHECK(header_lit > 0);

    // ...and there is no ladder, which is the honest answer.
    for (int i = 0; i < kLevels; ++i) {
        CHECK(c.count_in_row(kSpreadRow - 1 - i, Ink::Background) == kPanelWidth);
        CHECK(c.count_in_row(kBidTop + i, Ink::Background) == kPanelWidth);
    }

    // The whole frame is grey, and none of it is black.
    const auto pal = palette_for(s.status);
    for (int y = 0; y < kPanelHeight; ++y) {
        for (int x = 0; x < kPanelWidth; ++x) {
            const auto rgb = pal[c.ink[y][x]];
            CHECK(rgb.grey());
            CHECK_FALSE(rgb.black());
        }
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
            lit += kPanelWidth - c.count_in_row(y, Ink::Background);
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
                REQUIRE_FALSE(pal.ink[k].black());
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
