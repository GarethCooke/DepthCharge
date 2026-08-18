// dc_harness/console_ladder.cpp — see console_ladder.hpp.
//
// Widths are tracked as they are written rather than measured afterwards: the
// box-drawing and block glyphs are multi-byte UTF-8 but one column wide, so
// std::string::size() would misalign every row. A tiny Row builder counts
// columns for us and treats colour escapes as zero width.
//
// The box width is chosen up front from the widest thing any row can contain,
// in one pass and without buffering rows — see `inner` in render_ladder. That
// matters beyond this file: M3 Stage D takes this renderer as the visual spec
// for the HUB75 panel, where invariant #7 forbids the obvious alternative of
// building every row, measuring, and padding in a second pass.
#include "dc_harness/console_ladder.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

#include <depthcharge/decimal.hpp>

#include "dc_harness/age_estimator.hpp"

namespace dc::harness {
namespace {

using depthcharge::BookLevel;
using depthcharge::DisplaySnapshot;
using depthcharge::GapReason;
using depthcharge::Qty;
using depthcharge::Side;

namespace ansi {
constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kDim = "\x1b[2m";
constexpr std::string_view kGrey = "\x1b[90m";
constexpr std::string_view kBid = "\x1b[32m";        // green
constexpr std::string_view kBidBest = "\x1b[1;92m";  // bright green
constexpr std::string_view kAsk = "\x1b[31m";        // red
constexpr std::string_view kAskBest = "\x1b[1;91m";  // bright red
constexpr std::string_view kFrame = "\x1b[38;5;244m";
constexpr std::string_view kLive = "\x1b[1;92m";
constexpr std::string_view kStale = "\x1b[1;93m";  // amber: the honest warning
constexpr std::string_view kTape = "\x1b[97m";
}  // namespace ansi

// The fixed text of the two rows whose width depends on their content. Named
// rather than inlined so the width calculation below and the drawing code
// cannot disagree about how long they are.
//
// `kVenue` USED TO BE THE LITERAL " ANVIL " AND IS NOW THE CALLER'S. It was
// correct for three milestones and wrong from the moment `run_replay` stopped
// refusing a second venue — a Kraken BTC/USD ladder headed ANVIL is not a
// cosmetic defect, it is the ladder lying about where its numbers came from,
// which is the same failure as a golden produced by the wrong parser one rung
// down. See LadderStyle::venue for why the name is a style field and not a
// DisplaySnapshot one.
constexpr std::string_view kTitle = " DEPTHCHARGE ";
constexpr std::string_view kSeqLabel = " seq ";
// The age meter (M4 stage A2). It sits in the header beside `seq` — a NUMBER,
// never a colour — because the 2026-08-17 ruling took book silence away from the
// staleness clock and gave the panel this instead. A reader must be able to see
// "live, and 90 seconds behind", which is a state this object had no way to
// draw for three milestones and which really happened on 2026-08-11.
constexpr std::string_view kAgeLabel = " age ";
constexpr std::string_view kBannerLead = "!! STALE - book unknown (";
constexpr std::string_view kBannerTail = ") - not live data";

// The header's right-hand run: " ● " before the status text, then a preferred
// column of breathing room before the border.
//
// The margin is deliberately NOT part of the width floor. The floor is what the
// header must have or it overflows the box; the margin is a nicety the fill
// stops short for and pad_to() reclaims when the row is otherwise short. That
// keeps a narrow "LIVE" header spaced off the frame while a wide "STALE
// awaiting snapshot" one simply uses the room — which is what the old
// status_cols arithmetic did when it happened to fit, and the box now grows to
// guarantee rather than leaving the header hanging outside it.
constexpr std::size_t kStatusLead = 3;    // ' ' + dot + ' '
constexpr std::size_t kStatusMargin = 1;  // preferred gap before the border

struct Glyphs {
    std::string_view tl, tr, bl, br, h, v, ml, mr;
    std::string_view fill, empty, stale_fill;
    std::string_view live_dot, stale_dot, up, down, times, sep;
};

// Designated rather than positional: seventeen same-typed members in a row is a
// transposition waiting to happen, and swapping two would still compile.
constexpr Glyphs kUnicode{
    .tl = "┌", .tr = "┐", .bl = "└", .br = "┘",
    .h = "─", .v = "│", .ml = "├", .mr = "┤",
    .fill = "█", .empty = "░", .stale_fill = "·",
    .live_dot = "●", .stale_dot = "▒", .up = "▲", .down = "▼", .times = "×", .sep = "·",
};
constexpr Glyphs kAscii{
    .tl = "+", .tr = "+", .bl = "+", .br = "+",
    .h = "-", .v = "|", .ml = "+", .mr = "+",
    .fill = "#", .empty = ".", .stale_fill = ":",
    .live_dot = "*", .stale_dot = "!", .up = "^", .down = "v", .times = "x", .sep = "-",
};

// A row under construction: `w` counts display columns, colour escapes add none.
// The colour flag is a member rather than an argument to esc(), which used to
// mean threading it through two dozen call sites.
class Row {
public:
    explicit Row(bool color) noexcept : color_(color) {}

    void esc(std::string_view code) {
        if (color_) { text_ += code; }
    }
    void glyph(std::string_view g) {  // one column, possibly multi-byte
        text_ += g;
        ++w_;
    }
    void put(std::string_view s) {  // ASCII only
        text_ += s;
        w_ += s.size();
    }
    // Kept as a loop: 25 of the 26 pads per ladder are a single column, where
    // append(n, ' ') does not inline and measures slower.
    void pad(std::size_t n) {
        while (n-- > 0) { put(" "); }
    }
    void pad_to(std::size_t width) {
        if (w_ < width) { pad(width - w_); }
    }
    void repeat(std::string_view g, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) { glyph(g); }
    }
    void fill_to(std::size_t width, std::string_view g) {
        while (w_ < width) { glyph(g); }
    }
    std::size_t width() const noexcept { return w_; }
    const std::string& str() const noexcept { return text_; }

private:
    std::string text_;
    std::size_t w_ = 0;
    bool color_;
};

// Everything a row needs that does not change within a frame.
struct Ctx {
    const Glyphs& g;
    bool color;
    bool stale;
    std::size_t inner;    // box interior width, in columns
    std::size_t bar_w;
    std::int32_t dp;      // price decimals
    std::int32_t qd;      // qty decimals
    Qty max_qty;
};

// A quantity, at the venue's declared step.
//
// THIS PRINTED THE RAW STEP COUNT UNTIL M4 STAGE B1, and it was correct only
// because every venue consumed so far had a qty step of 1. Anvil quotes whole
// units, so the integer in the book WAS the human number. Kraken's step is
// 1e-08: a resting size of 0.65540712 BTC is 65,540,712 steps, and the old
// function drew that number in the size column beside a price of 62,818.8 — off
// by eight orders of magnitude, perfectly legible, and wrong in a way no test
// asserted because no test had a venue with a step to get wrong.
//
// Integer-only, via the engine's own formatter, exactly as the price column is:
// the ladder must not be able to disagree with the book (invariant #3).
std::string qty_text(Qty q, std::int32_t decimals) {
    char buf[depthcharge::kMaxFormattedChars] = {};
    const std::size_t n = depthcharge::format_scaled(q, decimals, buf, sizeof buf);
    if (n == 0) { return "?"; }
    return std::string(buf, n);
}

// Right-align within a fixed field.
void put_right(Row& row, std::string_view s, std::size_t width) {
    if (s.size() < width) { row.pad(width - s.size()); }
    row.put(s);
}

// Right-align so the text *ends* at `end_col`. Stated as an absolute column
// rather than a computed field width: the width form would be
// `end_col - row.width() - s.size()`, which underflows into a ~2^64 pad loop
// the moment a caller's earlier columns grow past it.
void put_right_at(Row& row, std::string_view s, std::size_t end_col) {
    while (row.width() + s.size() < end_col) { row.put(" "); }
    row.put(s);
}

std::string_view reason_text(GapReason r) {
    switch (r) {
        case GapReason::SeqGap:       return "seq gap";
        case GapReason::ChecksumFail: return "checksum fail";
        case GapReason::Disconnect:   return "disconnect";
        case GapReason::Overflow:     return "overflow";
        case GapReason::Resync:       return "awaiting snapshot";
    }
    return "unknown";
}

// Largest quantity in the drawn window; the bars are scaled to it so the shape
// of the visible book is readable regardless of the venue's absolute sizes.
//
// Seeded at 1, and each side guarded for emptiness: a book that has adopted
// nothing renders through here (test_console_ladder.cpp covers it), and
// std::ranges::max over an empty range is undefined behaviour, not an error.
Qty window_max_qty(const DisplaySnapshot& s, std::size_t levels) {
    const auto side_max = [levels](const BookLevel* side, std::size_t count) {
        const std::size_t n = std::min(count, levels);
        Qty m = 0;
        for (std::size_t i = 0; i < n; ++i) { m = std::max(m, side[i].qty); }
        return m;
    };
    return std::max({Qty{1}, side_max(s.bids, s.bid_count), side_max(s.asks, s.ask_count)});
}

void draw_bar(Row& row, const Ctx& ctx, Qty qty) {
    std::size_t filled = 0;
    if (qty > 0 && ctx.max_qty > 0) {
        filled = static_cast<std::size_t>((static_cast<std::int64_t>(ctx.bar_w) * qty) /
                                          ctx.max_qty);
        if (filled == 0) { filled = 1; }  // a level that exists is never invisible
        filled = std::min(filled, ctx.bar_w);
    }
    // Stale bars are hatched, not solid: the ladder is legible but obviously
    // not a live depth profile, even with colour stripped out.
    row.repeat(ctx.stale ? ctx.g.stale_fill : ctx.g.fill, filled);
    row.repeat(ctx.stale ? " " : ctx.g.empty, ctx.bar_w - filled);
}

// Every row ends the same way: back to frame colour, pad out to the box width,
// the right-hand border, reset. Six copies of that became one.
void close_row(Row& r, const Ctx& ctx, std::string_view border) {
    r.esc(ansi::kFrame);
    r.pad_to(ctx.inner);
    r.glyph(border);
    r.esc(ansi::kReset);
}

// One price level. The ask and bid loops were the same fourteen lines differing
// only in side, colour pair and iteration direction.
void level_row(Row& r, const Ctx& ctx, const BookLevel& lvl, bool best, bool is_bid) {
    r.esc(ansi::kFrame);
    r.glyph(ctx.g.v);
    const std::string_view tone =
        ctx.stale ? ansi::kGrey
                  : (is_bid ? (best ? ansi::kBidBest : ansi::kBid)
                            : (best ? ansi::kAskBest : ansi::kAsk));
    r.esc(tone);
    r.put(is_bid ? " B " : " A ");
    put_right(r, format_px(lvl.px, ctx.dp), 10);
    r.put(" ");
    draw_bar(r, ctx, lvl.qty);
    // Quantities right-align against the frame, so the column reads as one
    // number no matter how wide the box is.
    put_right_at(r, qty_text(lvl.qty, ctx.qd), ctx.inner - 1);
    close_row(r, ctx, ctx.g.v);
}

}  // namespace

std::string format_px(depthcharge::PriceTicks px, std::int32_t decimals) {
    char buf[depthcharge::kMaxFormattedChars] = {};
    const std::size_t n = depthcharge::format_scaled(px, decimals, buf, sizeof buf);
    // A price the declared scale cannot render is a configuration error, not an
    // empty column. Say so where it will be seen: a blank price reads as "no
    // level here", which is the one thing the ladder must never imply falsely.
    if (n == 0) { return "?"; }
    return std::string(buf, n);
}

std::string ladder_home(const LadderStyle& style) {
    return style.color ? "\x1b[H\x1b[J" : std::string{};
}

std::string render_ladder(const DisplaySnapshot& snap, const LadderStyle& style) {
    const Glyphs& g = style.unicode ? kUnicode : kAscii;
    const bool c = style.color;
    const bool stale = !snap.live();
    const std::size_t levels = std::min(style.levels, depthcharge::kDisplayLevels);
    const std::size_t bar_w = std::max<std::size_t>(style.bar_width, 4);
    const std::int32_t dp = snap.symbol.price_decimals;
    const std::int32_t qd = snap.symbol.qty_decimals;

    // The venue field, and the instrument. Both are the caller's to supply; an
    // absent venue draws nothing rather than a default that has not been
    // checked, and an absent symbol falls back to the integer id.
    const std::string venue_text =
        style.venue.empty() ? std::string{} : " " + std::string(style.venue) + " ";
    const std::string id_text = style.symbol.empty()
                                    ? std::to_string(snap.symbol.id)
                                    : std::string(style.symbol);
    const std::string seq_text = std::to_string(static_cast<unsigned long long>(snap.seq));
    // "-" until the estimator has a window: "no reading yet" and "the book is
    // current" are different statements, and printing 0.0s for the first is the
    // reassuring one of the two (age_estimator.hpp).
    const std::string age_text =
        snap.has_age ? std::string(AgeText(snap.age_ms).buf) : std::string("-");
    const std::string_view reason = reason_text(snap.stale_reason);
    const std::string status =
        stale ? std::string("STALE ") + std::string(reason) : std::string("LIVE");

    // The box has to be at least as wide as the widest row it will contain.
    // Level rows set a floor of 23 + bar_w, and 56 keeps the tape row able to
    // hold more than one print — but the header and the stale banner both grow
    // with the seq digits and the gap reason, and used to overflow the box
    // silently (a 62-column header inside a 57-column frame at boot).
    //
    // Both are computed here from the same string_views the drawing code uses,
    // so they cannot drift apart. tl + h + title + sep + venue + id + ' ' + sep
    // + seq label + seq + ' ', then ' ' + dot + ' ' + status.
    const std::size_t header_cols = 2 + kTitle.size() + 1 + venue_text.size() + id_text.size() +
                                    1 + 1 + kSeqLabel.size() + seq_text.size() + 1 +
                                    1 + kAgeLabel.size() + age_text.size() + 1 +
                                    kStatusLead + status.size();
    // v + ' ' + lead + reason + tail.
    const std::size_t banner_cols =
        stale ? 2 + kBannerLead.size() + reason.size() + kBannerTail.size() : 0;
    const std::size_t inner =
        std::max({23 + bar_w, std::size_t{56}, header_cols, banner_cols});

    const Ctx ctx{g, c, stale, inner, bar_w, dp, qd, window_max_qty(snap, levels)};

    std::string out;
    const auto line = [&out](const Row& r) {
        out += r.str();
        out += '\n';
    };

    // --- header -------------------------------------------------------------
    {
        Row r(c);
        r.esc(ansi::kFrame);
        r.glyph(g.tl);
        r.glyph(g.h);
        r.put(kTitle);
        r.esc(ansi::kReset);
        r.glyph(g.sep);
        r.put(venue_text);
        r.put(id_text);
        r.put(" ");
        r.glyph(g.sep);
        r.put(kSeqLabel);
        r.put(seq_text);
        r.put(" ");
        r.glyph(g.sep);
        r.put(kAgeLabel);
        r.put(age_text);
        r.put(" ");
        r.esc(ansi::kFrame);
        // Status sits hard against the right edge, where the eye lands last.
        const std::size_t want = kStatusLead + status.size() + kStatusMargin;
        r.fill_to(inner - std::min(inner, want), g.h);
        r.put(" ");
        r.esc(stale ? ansi::kStale : ansi::kLive);
        r.glyph(stale ? g.stale_dot : g.live_dot);
        r.put(" ");
        r.put(status);
        close_row(r, ctx, g.tr);
        line(r);
    }

    // --- stale banner -------------------------------------------------------
    // Channel two of the three: the word STALE on its own line, present only
    // when the book is not to be trusted (invariant #5).
    if (stale) {
        Row r(c);
        r.esc(ansi::kFrame);
        r.glyph(g.v);
        r.esc(ansi::kStale);
        r.put(" ");
        r.put(kBannerLead);
        r.put(reason);
        r.put(kBannerTail);
        close_row(r, ctx, g.v);
        line(r);
    }

    // --- asks, worst first so the touch meets the spread row ----------------
    const std::size_t n_ask = std::min<std::size_t>(levels, snap.ask_count);
    for (std::size_t k = n_ask; k > 0; --k) {
        const std::size_t i = k - 1;
        Row r(c);
        level_row(r, ctx, snap.asks[i], /*best=*/i == 0, /*is_bid=*/false);
        line(r);
    }

    // --- the spread gap -----------------------------------------------------
    {
        Row r(c);
        r.esc(ansi::kFrame);
        r.glyph(g.ml);
        r.glyph(g.h);
        if (snap.has_top()) {
            r.put(" spread ");
            r.put(format_px(snap.spread_ticks(), dp));
            r.put(" (");
            r.put(std::to_string(static_cast<long long>(snap.spread_ticks())));
            r.put(snap.spread_ticks() == 1 ? " tick)" : " ticks)");
        } else {
            r.put(" no book ");
        }
        if (snap.has_last) {
            r.esc(ansi::kTape);
            r.put("  last ");
            r.put(format_px(snap.last_px, dp));
            r.esc(ansi::kFrame);
        }
        r.put(" ");
        r.fill_to(inner, g.h);
        r.glyph(g.mr);
        r.esc(ansi::kReset);
        line(r);
    }

    // --- bids, best first ---------------------------------------------------
    const std::size_t n_bid = std::min<std::size_t>(levels, snap.bid_count);
    for (std::size_t i = 0; i < n_bid; ++i) {
        Row r(c);
        level_row(r, ctx, snap.bids[i], /*best=*/i == 0, /*is_bid=*/true);
        line(r);
    }

    // --- trade tape ---------------------------------------------------------
    if (style.show_tape) {
        Row r(c);
        r.esc(ansi::kFrame);
        r.glyph(g.v);
        r.esc(stale ? ansi::kDim : ansi::kTape);
        r.put(" tape ");
        if (snap.trade_count == 0) {
            r.esc(ansi::kDim);
            r.put("(no prints yet)");
        }
        for (std::size_t i = 0; i < snap.trade_count; ++i) {
            const auto& t = snap.trades[i];
            const std::string px = format_px(t.px, dp);
            const std::string qty = qty_text(t.qty, ctx.qd);
            // Aggressor direction, spelled twice over (arrow + letter) so the
            // tape survives an ASCII terminal.
            const bool buy = t.aggressor == Side::Bid;
            if (r.width() + px.size() + qty.size() + 4 > inner) { break; }
            r.esc(stale ? ansi::kGrey : (buy ? ansi::kBid : ansi::kAsk));
            r.put(px);
            r.glyph(g.times);
            r.put(qty);
            r.glyph(buy ? g.up : g.down);
            r.put(" ");
            r.esc(stale ? ansi::kDim : ansi::kTape);
        }
        close_row(r, ctx, g.v);
        line(r);
    }

    // --- footer -------------------------------------------------------------
    {
        Row r(c);
        r.esc(ansi::kFrame);
        r.glyph(g.bl);
        r.fill_to(inner, g.h);
        r.glyph(g.br);
        r.esc(ansi::kReset);
        line(r);
    }

    return out;
}

}  // namespace dc::harness
