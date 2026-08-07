// dc_harness/console_ladder.cpp — see console_ladder.hpp.
//
// Widths are tracked as they are written rather than measured afterwards: the
// box-drawing and block glyphs are multi-byte UTF-8 but one column wide, so
// std::string::size() would misalign every row. A tiny Row builder counts
// columns for us and treats colour escapes as zero width.
#include "dc_harness/console_ladder.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

#include <depthcharge/decimal.hpp>

namespace dc::harness {
namespace {

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

struct Glyphs {
    std::string_view tl, tr, bl, br, h, v, ml, mr;
    std::string_view fill, empty, stale_fill;
    std::string_view live_dot, stale_dot, up, down, times, sep;
};

constexpr Glyphs kUnicode{"┌", "┐", "└", "┘", "─", "│", "├", "┤",
                          "█", "░", "·",
                          "●", "▒", "▲", "▼", "×", "·"};
constexpr Glyphs kAscii{"+", "+", "+", "+", "-", "|", "+", "+",
                        "#", ".", ":",
                        "*", "!", "^", "v", "x", "-"};

// A row under construction: `w` counts display columns, colour escapes add none.
class Row {
public:
    void esc(std::string_view code, bool enabled) {
        if (enabled) { text_ += code; }
    }
    void glyph(std::string_view g) {  // one column, possibly multi-byte
        text_ += g;
        ++w_;
    }
    void put(std::string_view s) {  // ASCII only
        text_ += s;
        w_ += s.size();
    }
    void repeat(std::string_view g, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) { glyph(g); }
    }
    void pad_to(std::size_t width) {
        while (w_ < width) { put(" "); }
    }
    void fill_to(std::size_t width, std::string_view g) {
        while (w_ < width) { glyph(g); }
    }
    std::size_t width() const { return w_; }
    const std::string& str() const { return text_; }

private:
    std::string text_;
    std::size_t w_ = 0;
};

std::string qty_text(Qty q) {
    return std::to_string(static_cast<long long>(q));
}

// Right-align within a fixed field.
void put_right(Row& row, std::string_view s, std::size_t width) {
    for (std::size_t i = s.size(); i < width; ++i) { row.put(" "); }
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

const char* reason_text(GapReason r) {
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
Qty window_max_qty(const DisplaySnapshot& s, std::size_t levels) {
    Qty m = 1;
    for (std::size_t i = 0; i < levels && i < s.bid_count; ++i) { m = std::max(m, s.bids[i].qty); }
    for (std::size_t i = 0; i < levels && i < s.ask_count; ++i) { m = std::max(m, s.asks[i].qty); }
    return m;
}

void draw_bar(Row& row, Qty qty, Qty max_qty, std::size_t width, bool stale, const Glyphs& g) {
    std::size_t filled = 0;
    if (qty > 0 && max_qty > 0) {
        filled = static_cast<std::size_t>((static_cast<std::int64_t>(width) * qty) / max_qty);
        if (filled == 0) { filled = 1; }  // a level that exists is never invisible
        filled = std::min(filled, width);
    }
    // Stale bars are hatched, not solid: the ladder is legible but obviously
    // not a live depth profile, even with colour stripped out.
    row.repeat(stale ? g.stale_fill : g.fill, filled);
    row.repeat(stale ? " " : g.empty, width - filled);
}

}  // namespace

std::string format_px(depthcharge::PriceTicks px, std::int32_t decimals) {
    char buf[depthcharge::kMaxFormattedChars] = {};
    const std::size_t n = depthcharge::format_scaled(px, decimals, buf, sizeof buf);
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
    // Wide enough for the level rows, and never so narrow that the tape row can
    // only hold one print.
    const std::size_t inner = std::max<std::size_t>(23 + bar_w, 56);
    const std::int32_t dp = snap.symbol.price_decimals;

    std::string out;
    const auto line = [&out](const Row& r) {
        out += r.str();
        out += '\n';
    };

    // --- header -------------------------------------------------------------
    {
        Row r;
        r.esc(ansi::kFrame, c);
        r.glyph(g.tl);
        r.glyph(g.h);
        r.put(" DEPTHCHARGE ");
        r.esc(ansi::kReset, c);
        r.glyph(g.sep);
        r.put(" ANVIL ");
        r.put(std::to_string(snap.symbol.id));
        r.put(" ");
        r.glyph(g.sep);
        r.put(" seq ");
        r.put(std::to_string(static_cast<unsigned long long>(snap.seq)));
        r.put(" ");
        r.esc(ansi::kFrame, c);
        // Status sits hard against the right edge, where the eye lands last.
        const std::string status = stale ? std::string("STALE ") + reason_text(snap.stale_reason)
                                         : std::string("LIVE");
        const std::size_t status_cols = status.size() + 4;  // dot + spaces + margin
        r.fill_to(inner > status_cols ? inner - status_cols : 0, g.h);
        r.put(" ");
        r.esc(stale ? ansi::kStale : ansi::kLive, c);
        r.glyph(stale ? g.stale_dot : g.live_dot);
        r.put(" ");
        r.put(status);
        r.esc(ansi::kFrame, c);
        r.pad_to(inner);
        r.glyph(g.tr);
        r.esc(ansi::kReset, c);
        line(r);
    }

    // --- stale banner -------------------------------------------------------
    // Channel two of the three: the word STALE on its own line, present only
    // when the book is not to be trusted (invariant #5).
    if (stale) {
        Row r;
        r.esc(ansi::kFrame, c);
        r.glyph(g.v);
        r.esc(ansi::kStale, c);
        r.put(" ");
        r.put("!! STALE - book unknown (");
        r.put(reason_text(snap.stale_reason));
        r.put(") - not live data");
        r.esc(ansi::kFrame, c);
        r.pad_to(inner);
        r.glyph(g.v);
        r.esc(ansi::kReset, c);
        line(r);
    }

    const Qty max_qty = window_max_qty(snap, levels);

    // --- asks, worst first so the touch meets the spread row ----------------
    const std::size_t n_ask = std::min<std::size_t>(levels, snap.ask_count);
    for (std::size_t k = n_ask; k > 0; --k) {
        const std::size_t i = k - 1;
        Row r;
        r.esc(ansi::kFrame, c);
        r.glyph(g.v);
        r.esc(stale ? ansi::kGrey : (i == 0 ? ansi::kAskBest : ansi::kAsk), c);
        r.put(" A ");
        put_right(r, format_px(snap.asks[i].px, dp), 10);
        r.put(" ");
        draw_bar(r, snap.asks[i].qty, max_qty, bar_w, stale, g);
        // Quantities right-align against the frame, so the column reads as one
        // number no matter how wide the box is.
        put_right_at(r, qty_text(snap.asks[i].qty), inner - 1);
        r.esc(ansi::kFrame, c);
        r.pad_to(inner);
        r.glyph(g.v);
        r.esc(ansi::kReset, c);
        line(r);
    }

    // --- the spread gap -----------------------------------------------------
    {
        Row r;
        r.esc(ansi::kFrame, c);
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
            r.esc(ansi::kTape, c);
            r.put("  last ");
            r.put(format_px(snap.last_px, dp));
            r.esc(ansi::kFrame, c);
        }
        r.put(" ");
        r.fill_to(inner, g.h);
        r.glyph(g.mr);
        r.esc(ansi::kReset, c);
        line(r);
    }

    // --- bids, best first ---------------------------------------------------
    const std::size_t n_bid = std::min<std::size_t>(levels, snap.bid_count);
    for (std::size_t i = 0; i < n_bid; ++i) {
        Row r;
        r.esc(ansi::kFrame, c);
        r.glyph(g.v);
        r.esc(stale ? ansi::kGrey : (i == 0 ? ansi::kBidBest : ansi::kBid), c);
        r.put(" B ");
        put_right(r, format_px(snap.bids[i].px, dp), 10);
        r.put(" ");
        draw_bar(r, snap.bids[i].qty, max_qty, bar_w, stale, g);
        put_right_at(r, qty_text(snap.bids[i].qty), inner - 1);
        r.esc(ansi::kFrame, c);
        r.pad_to(inner);
        r.glyph(g.v);
        r.esc(ansi::kReset, c);
        line(r);
    }

    // --- trade tape ---------------------------------------------------------
    if (style.show_tape) {
        Row r;
        r.esc(ansi::kFrame, c);
        r.glyph(g.v);
        r.esc(stale ? ansi::kDim : ansi::kTape, c);
        r.put(" tape ");
        if (snap.trade_count == 0) {
            r.esc(ansi::kDim, c);
            r.put("(no prints yet)");
        }
        for (std::size_t i = 0; i < snap.trade_count; ++i) {
            const auto& t = snap.trades[i];
            const std::string px = format_px(t.px, dp);
            const std::string qty = qty_text(t.qty);
            // Aggressor direction, spelled twice over (arrow + letter) so the
            // tape survives an ASCII terminal.
            const bool buy = t.aggressor == Side::Bid;
            if (r.width() + px.size() + qty.size() + 4 > inner) { break; }
            r.esc(stale ? ansi::kGrey : (buy ? ansi::kBid : ansi::kAsk), c);
            r.put(px);
            r.glyph(g.times);
            r.put(qty);
            r.glyph(buy ? g.up : g.down);
            r.put(" ");
            r.esc(stale ? ansi::kDim : ansi::kTape, c);
        }
        r.esc(ansi::kFrame, c);
        r.pad_to(inner);
        r.glyph(g.v);
        r.esc(ansi::kReset, c);
        line(r);
    }

    // --- footer -------------------------------------------------------------
    {
        Row r;
        r.esc(ansi::kFrame, c);
        r.glyph(g.bl);
        r.fill_to(inner, g.h);
        r.glyph(g.br);
        r.esc(ansi::kReset, c);
        line(r);
    }

    return out;
}

}  // namespace dc::harness
