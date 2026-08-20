// firmware/src/ladder_render.hpp — the 64x64 ladder, and the honesty bit made structural.
//
// M3 stage D. The M1 console ladder (harness/src/console_ladder.cpp) is the
// visual spec; this is the same DisplaySnapshot at 64x64 with one pixel per
// level. Everything here is ESP-IDF-free and host-tested
// (harness/tests/test_ladder_render.cpp), following the precedent of
// frame_reassembler.hpp: the only firmware code the bench cannot check on a desk
// should be the code that touches a peripheral.
//
// ============================================================================
// INVARIANT #5, AND WHY IT IS A static_assert RATHER THAN A RULE
// ============================================================================
//
// "status == Stale greys the whole panel" is the point of the milestone, and the
// obvious implementation — check snap.live() wherever a colour is chosen — is a
// rule somebody has to keep. It fails the first time a session adds a widget and
// forgets, and it fails silently, drawing a coloured ladder off a book nobody
// should trust. That is the one output ARCHITECTURE calls unacceptable.
//
// So this renderer CANNOT NAME A COLOUR. It emits `Ink` — a role, not an RGB —
// and the canvas it draws into holds the Palette. The palette is selected exactly
// once, from snap.status, where the canvas is built (render_task.cpp), and
// `kStalePalette` is proven to be entirely on the grey ramp AT COMPILE TIME:
//
//     static_assert(all_grey(kStalePalette), ...)
//
// A new Ink added without a stale entry fails the build. A stale entry given a
// hue fails the build. There is no path from a stale snapshot to a coloured pixel
// that the compiler will accept, which is a stronger statement than any test.
//
// The one thing status DOES steer here is TEXT — the header shows the stale
// reason instead of the last price. That is disclosure, not decoration, and
// test_ladder_render.cpp pins the complement: for the same book, the Ink grid
// over the ladder region is IDENTICAL for Live and Stale. Geometry stays, hue
// goes.
//
// ============================================================================
// THE ROW BUDGET — checked here, not on the panel
// ============================================================================
//
//   row  0.. 4   header      (kGlyphHeight, one line of ladder_font.hpp)
//   row      5   rule
//   row  6..32   asks, worst at the top, asks[0] adjacent to the spread
//   row     33   the spread gap
//   row 34..60   bids, bids[0] adjacent to the spread, worst at the bottom
//   row     61   rule
//   row 62..63   tape strip: last-price sparkline, and the render heartbeat
//
// kLevels falls out of that arithmetic rather than being stated, and is clamped
// to kDisplayLevels. If the header ever needs a second line the ladder loses
// levels and the build stays green — which is exactly the brief's instruction
// ("draw fewer levels; do NOT change kDisplayLevels", that being a §5 change to
// engine/). It cannot silently overrun the panel: kStripRows would go negative
// and the static_assert below fires.
//
// INVARIANT #3. Bar lengths are `qty * width / max_qty` in int64. No float
// touches book data; the only float-shaped thing on this path is
// depthcharge::format_scaled, which is integer arithmetic producing text.
//
// INVARIANT #7. No allocation, no String, no std::string, no printf. Every
// buffer is a fixed-size automatic; LadderView's sparkline ring is a member.
#pragma once

#include <cstddef>
#include <cstdint>

#include <depthcharge/decimal.hpp>
#include <depthcharge/age_estimator.hpp>
#include <depthcharge/display_snapshot.hpp>

#include "ladder_font.hpp"

namespace depthcharge::fw {

// ---------------------------------------------------------------------------
// Ink — what the renderer is allowed to say about colour, which is nothing
// ---------------------------------------------------------------------------

enum class Ink : std::uint8_t {
    Background = 0,  // every pixel the ladder does not claim
    Chrome,          // the two rules above and below the ladder
    Symbol,          // header: which instrument
    Value,           // header: last price when live, stale reason when not
    Bid,
    BidBest,         // bids[0], hard against the spread
    Ask,
    AskBest,         // asks[0], hard against the spread
    Spread,          // the one-row gap between the two sides
    Tape,            // the last-price sparkline
    Flash,           // a level a trade just printed at
    Beat,            // the render heartbeat pixel
    Count
};

inline constexpr std::size_t kInkCount = static_cast<std::size_t>(Ink::Count);

constexpr std::size_t ink_index(Ink i) noexcept { return static_cast<std::size_t>(i); }

struct Rgb {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};

    constexpr bool grey() const noexcept { return r == g && g == b; }
    constexpr bool black() const noexcept { return r == 0 && g == 0 && b == 0; }
};

struct Palette {
    Rgb ink[kInkCount];

    constexpr Rgb operator[](Ink i) const noexcept { return ink[ink_index(i)]; }
};

namespace detail {

// Assigned BY NAME rather than positionally. Twelve same-typed members in a row
// is a transposition that still compiles — the console ladder's Glyphs struct
// took designated initialisers for the same reason, and a C array cannot.
// THE HUES ARE PURE, AND THAT IS A SIX-BIT DECISION.
//
// The first version tinted the sides towards white to mark the touch —
// Ask {160,20,20}, AskBest {255,60,60}. At the library's default eight-bit depth
// that reads as a brighter red. At the six bits this build actually uses (see
// kMaxColourDepth), the small green and blue components survive quantisation
// while the red is already saturated, and the bench verdict was immediate:
// **"red is now more pink"**.
//
// So each side is one channel and nothing else, and best-of-book is distinguished
// by INTENSITY rather than by tint. A pure channel cannot drift towards another
// hue however coarsely it is quantised, which is the property worth having on a
// panel whose entire job is that green and red mean opposite things.
constexpr Palette make_live_palette() noexcept {
    Palette p{};
    p.ink[ink_index(Ink::Background)] = {0, 0, 0};
    p.ink[ink_index(Ink::Chrome)] = {30, 30, 45};
    p.ink[ink_index(Ink::Symbol)] = {0, 160, 200};
    p.ink[ink_index(Ink::Value)] = {255, 255, 255};
    // BEST-OF-BOOK IS 255 ON PURPOSE, AND IT IS NOT ABOUT BRIGHTNESS.
    //
    // This panel is 1/32 scan: row y and row y+32 are shifted out in the SAME
    // slot, from one 16-bit word, upper half on R1/G1/B1 and lower half on
    // R2/G2/B2. The header is rows 0-5, so it is paired with rows 32-37 —
    // asks[0], the spread, and bids[0..3]. The touch.
    //
    // Under BCM, a channel at 255 is on for EVERY bit-plane of the frame: DC, no
    // transitions. An intermediate value is on for only some planes, so it
    // switches several times per 9.5 ms frame, and every one of those edges is a
    // transition on a data line running beside the other half's data line down a
    // bare ribbon.
    //
    // That was established the expensive way. The header flickered while being
    // completely static (Anvil trades roughly every 8 s), and it stopped dead the
    // moment the feed went stale and the bars froze — so it was coupled to the
    // neighbours, not to itself. The obvious reading was current draw, so the
    // bars were cut ~25% to reduce it. IT GOT WORSE, and gained pixels lit just
    // outside the glyphs. Turning 255 into 200 does not reduce activity on
    // asks[0] and bids[0] — the two rows paired with the header — it converts
    // them from DC to switching, and the ghosts arrived with the edges.
    //
    // So the aggressor is edges, not amps, and best-of-book stays pinned at the
    // rail. The remaining artifact is signal integrity on a bare-jumper
    // interconnect, which hardware/BRINGUP.md's forward note already calls out
    // as the thing to scope before suspecting firmware.
    p.ink[ink_index(Ink::Bid)] = {0, 150, 0};
    p.ink[ink_index(Ink::BidBest)] = {0, 255, 0};
    p.ink[ink_index(Ink::Ask)] = {170, 0, 0};
    p.ink[ink_index(Ink::AskBest)] = {255, 0, 0};
    p.ink[ink_index(Ink::Spread)] = {90, 70, 0};
    p.ink[ink_index(Ink::Tape)] = {0, 150, 200};
    p.ink[ink_index(Ink::Flash)] = {255, 255, 255};
    p.ink[ink_index(Ink::Beat)] = {0, 110, 150};
    return p;
}

// GREY, NEVER BLANK. A dark panel is ambiguous with "powered off" and says
// nothing where invariant #5 requires it to say "not trusted", so the stale
// BACKGROUND glows rather than sitting at zero. 64/255 was chosen to survive
// either answer to a question this file should not depend on: the HUB75 driver
// applies a CIE1931 lightness curve unless NO_CIE1931 is defined, so 64 is ~5%
// luminance with it and ~25% without — visibly lit both ways, and comfortably
// under M2's 2.6 A full-white measurement on a 5 V/5 A supply either way.
// It is what makes the boot frame — header, chrome, no ladder at all — read as
// an honest empty panel rather than a dead one. The bars sit well above it, so
// the ladder's shape stays readable through the wash.
// The wash stays where it is and EVERYTHING ELSE MOVES UP, which is the other
// half of the six-bit correction. The bench could not read the stale reason
// against the background: the whole ramp had been chosen at eight bits, where
// 215 against 64 is ample, and at six bits the gap closes enough that a 3x5 glyph
// stops carrying. Lowering the background would have been the wrong fix — it is
// the "this panel is ON, and not to be trusted" signal, and the boot frame has
// nothing else — so the text goes to full white and the ladder inks lift instead.
constexpr Palette make_stale_palette() noexcept {
    Palette p{};
    p.ink[ink_index(Ink::Background)] = {64, 64, 64};
    p.ink[ink_index(Ink::Chrome)] = {130, 130, 130};
    p.ink[ink_index(Ink::Symbol)] = {175, 175, 175};
    p.ink[ink_index(Ink::Value)] = {255, 255, 255};
    p.ink[ink_index(Ink::Bid)] = {140, 140, 140};
    p.ink[ink_index(Ink::BidBest)] = {210, 210, 210};
    p.ink[ink_index(Ink::Ask)] = {140, 140, 140};
    p.ink[ink_index(Ink::AskBest)] = {210, 210, 210};
    p.ink[ink_index(Ink::Spread)] = {100, 100, 100};
    p.ink[ink_index(Ink::Tape)] = {190, 190, 190};
    p.ink[ink_index(Ink::Flash)] = {255, 255, 255};
    p.ink[ink_index(Ink::Beat)] = {190, 190, 190};
    return p;
}

}  // namespace detail

inline constexpr Palette kLivePalette = detail::make_live_palette();
inline constexpr Palette kStalePalette = detail::make_stale_palette();

constexpr bool all_grey(const Palette& p) noexcept {
    for (std::size_t i = 0; i < kInkCount; ++i) {
        if (!p.ink[i].grey()) { return false; }
    }
    return true;
}

// `allowed` names the one Ink permitted to be pure black; pass Ink::Count for
// "none". Doubles as the check that no Ink was left unassigned, since an
// unassigned entry is {0,0,0}.
constexpr bool none_black_except(const Palette& p, Ink allowed) noexcept {
    for (std::size_t i = 0; i < kInkCount; ++i) {
        if (i == ink_index(allowed)) { continue; }
        if (p.ink[i].black()) { return false; }
    }
    return true;
}

// THE INVARIANT #5 PROOFS. Not tests — the build does not complete without them.
static_assert(all_grey(kStalePalette),
              "invariant #5: a stale panel carries no hue anywhere. Geometry stays, hue goes.");
static_assert(!all_grey(kLivePalette),
              "the live palette must actually carry hue, or Live and Stale look the same");
static_assert(none_black_except(kStalePalette, Ink::Count),
              "grey, NEVER blank: every stale Ink must be visible, background included");
static_assert(none_black_except(kLivePalette, Ink::Background),
              "an Ink left unassigned in the live palette would draw as invisible black");

// THE ONE PALETTE SELECTION. Every colour on the panel comes through here, and
// nothing else in the firmware reads snap.status to pick one.
constexpr Palette palette_for(FeedStatus status) noexcept {
    return status == FeedStatus::Live ? kLivePalette : kStalePalette;
}

// ---------------------------------------------------------------------------
// Geometry — derived, asserted, and clamped to what engine/ publishes
// ---------------------------------------------------------------------------

inline constexpr int kPanelWidth = 64;
inline constexpr int kPanelHeight = 64;

inline constexpr int kHeaderRows = kGlyphHeight;  // one line of ladder_font
inline constexpr int kRuleRows = 1;
inline constexpr int kSpreadRows = 1;
inline constexpr int kMinStripRows = 2;  // the tape strip must survive the budget

// How many levels a side the leftover rows can hold, then clamped to what the
// book actually publishes. 27 today, from (64 - 5 - 2 - 1 - 2) / 2.
inline constexpr int kLevelBudget =
    (kPanelHeight - kHeaderRows - 2 * kRuleRows - kSpreadRows - kMinStripRows) / 2;
inline constexpr int kLevels =
    kLevelBudget < static_cast<int>(kDisplayLevels) ? kLevelBudget
                                                    : static_cast<int>(kDisplayLevels);

inline constexpr int kHeaderTop = 0;
inline constexpr int kTopRuleRow = kHeaderTop + kHeaderRows;    // 5
inline constexpr int kAskTop = kTopRuleRow + kRuleRows;         // 6
inline constexpr int kSpreadRow = kAskTop + kLevels;            // 33
inline constexpr int kBidTop = kSpreadRow + kSpreadRows;        // 34
inline constexpr int kBottomRuleRow = kBidTop + kLevels;        // 61
inline constexpr int kStripTop = kBottomRuleRow + kRuleRows;    // 62
inline constexpr int kStripRows = kPanelHeight - kStripTop;     // 2

static_assert(kLevels >= 1, "the row budget leaves no ladder at all");
static_assert(kStripRows >= kMinStripRows,
              "the header/chrome no longer fit above 2 x kLevels — draw fewer levels here, "
              "never change kDisplayLevels (that is engine/, ARCHITECTURE §5)");
static_assert(kStripTop + kStripRows == kPanelHeight, "the row budget must consume exactly 64 rows");
static_assert(kLevels <= static_cast<int>(kDisplayLevels), "cannot draw levels the book never fills");

// The heartbeat owns the bottom-right pixel; the sparkline stops two columns
// short of it so a moving tape can never be confused with a stalled renderer.
inline constexpr int kBeatX = kPanelWidth - 1;
inline constexpr int kBeatY = kPanelHeight - 1;
inline constexpr int kSparkColumns = kPanelWidth - 2;

// ---------------------------------------------------------------------------
// The canvas contract
// ---------------------------------------------------------------------------
//
// Anything with these two members, and no way to express a colour:
//
//     void hline(int x, int y, int w, Ink ink) noexcept;   // w >= 1
//     void pixel(int x, int y, Ink ink) noexcept;
//
// On the target it is PanelCanvas (panel.hpp), which resolves Ink through the
// palette it was built with and calls the HUB75 driver. In dc_tests it is a
// 64x64 grid of Ink plus a write-count, which is what lets the tests assert
// coverage and geometry rather than pixels-that-looked-right.

// ---------------------------------------------------------------------------
// Small helpers, all integer (invariant #3)
// ---------------------------------------------------------------------------

// Largest quantity in the drawn window, both sides. Seeded at 0 and guarded at
// the call site, exactly as the console ladder does it: a book that has adopted
// nothing must render, and it must not divide by anything.
constexpr Qty window_max_qty(const DisplaySnapshot& s) noexcept {
    Qty m = 0;
    const int nb = s.bid_count < kLevels ? static_cast<int>(s.bid_count) : kLevels;
    const int na = s.ask_count < kLevels ? static_cast<int>(s.ask_count) : kLevels;
    for (int i = 0; i < nb; ++i) {
        if (s.bids[i].qty > m) { m = s.bids[i].qty; }
    }
    for (int i = 0; i < na; ++i) {
        if (s.asks[i].qty > m) { m = s.asks[i].qty; }
    }
    return m;
}

// len = qty * width / max_qty, integer throughout, never rounded through a
// double (invariant #3). A level that exists is never invisible — one pixel is
// the floor, the same rule the console ladder applies, because an empty row and
// a tiny row must not read the same.
//
// THE MULTIPLY CAN OVERFLOW, AND `Qty` IS int64_t. The obvious
// `kPanelWidth * qty / max_qty` is undefined behaviour once qty passes
// INT64_MAX / 64 — around 1.4e17, which no venue we consume will ever quote, but
// `Qty` is the full 64-bit type by deliberate design (ARCHITECTURE §4: "crypto
// tick sizes and price ranges vary wildly"), the adapter's only quantity guard
// is `raw < 0`, and a renderer is not the place to find out. Found by the
// property test rather than by reading, which is the argument for having
// written it.
//
// The fix is to shift both operands down together until the product is safe.
// Both stay >= 1 (max_qty > qty > 0 at that point, and halving preserves the
// order), and the precision lost is far below one pixel of a 64-pixel bar.
constexpr int bar_length(Qty qty, Qty max_qty) noexcept {
    if (qty <= 0 || max_qty <= 0) { return 0; }
    if (qty >= max_qty) { return kPanelWidth; }

    constexpr std::int64_t kSafeMax = INT64_MAX / kPanelWidth;
    std::int64_t num = static_cast<std::int64_t>(qty);
    std::int64_t den = static_cast<std::int64_t>(max_qty);
    while (num > kSafeMax) {
        num >>= 1;
        den >>= 1;
    }

    std::int64_t len = (static_cast<std::int64_t>(kPanelWidth) * num) / den;
    if (len < 1) { len = 1; }
    if (len > kPanelWidth) { len = kPanelWidth; }
    return static_cast<int>(len);
}

// EIGHT CHARACTERS, MAXIMUM. The 4x6 font fits twelve across the header, and
// three of them plus a gap belong to the symbol id — so DISCONNECT, which fitted
// at 3x5, does not. NO LINK says the same thing to someone standing in front of
// a grey panel, and the serial log still prints the full `STALE (disconnect)`
// for anyone reading the run afterwards.
//
// test_ladder_render.cpp asserts every one of these against the real header
// width, so a longer word cannot be added without the desk saying so.
constexpr const char* reason_text(GapReason r) noexcept {
    switch (r) {
        case GapReason::SeqGap:       return "SEQ GAP";
        case GapReason::ChecksumFail: return "CHECKSUM";
        case GapReason::Disconnect:   return "NO LINK";
        case GapReason::Overflow:     return "OVERFLOW";
        case GapReason::Resync:       return "RESYNC";
    }
    return "STALE";
}

// A fixed stack buffer for one header field. kMaxFormattedChars is
// format_scaled's own worst case, so this cannot be outgrown by a wider price.
struct TextField {
    char buf[kMaxFormattedChars + 1]{};

    constexpr TextField() noexcept = default;

    // Integer -> text at a declared scale. decimals == 0 renders a plain
    // integer, which is how the symbol id gets drawn without a second formatter.
    static TextField scaled(std::int64_t value, int decimals) noexcept {
        TextField t;
        const std::size_t n = format_scaled(value, decimals, t.buf, sizeof t.buf - 1);
        if (n == 0) {
            t.buf[0] = '?';
            t.buf[1] = '\0';
        } else {
            t.buf[n] = '\0';
        }
        return t;
    }
};

// One side of the book, described rather than coded twice. Bids and asks differ
// in exactly three things — which array, which way the rows run away from the
// spread, and which ink pair — and in nothing else. Two near-identical loops
// would put that asymmetry in two places, and getting a side's direction or ink
// wrong is the single most misleading thing this renderer could do: the panel
// would look entirely plausible and be upside down.
//
// Designated initialisers at the call site for the same reason the console
// ladder's Glyphs struct takes them: six same-shaped fields in a row is a
// transposition that still compiles.
struct SidePlan {
    const BookLevel* levels;
    int count;
    int touch_row;  // where levels[0] sits — hard against the spread
    int step;       // +1 runs down the panel (bids), -1 runs up it (asks)
    Ink best;       // levels[0]
    Ink rest;
};

// ---------------------------------------------------------------------------
// LadderView — the render side's own memory
// ---------------------------------------------------------------------------
//
// The sparkline history and the trade flash are RENDER-SIDE SAMPLED STATE, held
// in a fixed ring in the render task. That is a decision the M3 brief pins and
// this comment exists to keep pinned: neither is ever a new DisplaySnapshot
// field, because that would be a §4/§5 change to the vocabulary two cores share
// for a purely cosmetic want. `last_px` legitimately advances while Stale (the
// tape is still real) and needs no special case — the grey wash covers it.
class LadderView {
public:
    // Sample the frame. Called once per consumed version, before draw().
    void observe(const DisplaySnapshot& snap) noexcept {
        if (have_seen_ && snap.version == last_version_) { return; }
        const bool first = !have_seen_;
        have_seen_ = true;
        last_version_ = snap.version;

        if (snap.has_last) {
            spark_[spark_head_] = snap.last_px;
            spark_head_ = static_cast<std::uint8_t>((spark_head_ + 1) % kSparkColumns);
            if (spark_count_ < kSparkColumns) { ++spark_count_; }
        }

        // A trade the ring has not shown before flashes its level for a few
        // frames. trades[0] is the newest and carries the event seq that
        // delivered it, so "new" is a comparison and not a heuristic — and on
        // the very first frame nothing flashes, because a boot is not a print.
        if (snap.trade_count > 0) {
            const Seq newest = snap.trades[0].seq;
            if (!first && newest != last_trade_seq_) {
                flash_px_ = snap.trades[0].px;
                flash_frames_ = kFlashFrames;
            }
            last_trade_seq_ = newest;
        }
    }

    template <class Canvas>
    void draw(const DisplaySnapshot& snap, Canvas& c) noexcept {
        // One normalisation for the whole frame. Computed here rather than per
        // side because the two sides sharing a scale is what makes the ladder a
        // picture of the book rather than two unrelated histograms.
        const Qty max_qty = window_max_qty(snap);

        draw_header(snap, c);
        c.hline(0, kTopRuleRow, kPanelWidth, Ink::Chrome);
        draw_side(c, SidePlan{snap.asks, snap.ask_count, kSpreadRow - 1, -1,
                              Ink::AskBest, Ink::Ask}, max_qty);
        c.hline(0, kSpreadRow, kPanelWidth, Ink::Spread);
        draw_side(c, SidePlan{snap.bids, snap.bid_count, kBidTop, +1,
                              Ink::BidBest, Ink::Bid}, max_qty);
        c.hline(0, kBottomRuleRow, kPanelWidth, Ink::Chrome);
        draw_strip(c);

        // Toggled every DRAWN frame, in the corner. Invariant #5 covers the feed
        // going quiet; nothing covers the renderer dying, and a dead render task
        // leaves precisely the frozen ladder ARCHITECTURE calls the one
        // unacceptable output. It doubles as a draw-rate read at the bench.
        beat_ = !beat_;
        if (flash_frames_ > 0) { --flash_frames_; }
    }

    bool beat() const noexcept { return beat_; }
    std::uint8_t spark_samples() const noexcept { return spark_count_; }
    bool flashing() const noexcept { return flash_frames_ > 0; }

private:
    // Three drawn frames is ~0.2 s at Anvil's publish rate: long enough to
    // register across a desk, short enough that a busy tape does not leave the
    // ladder permanently white.
    static constexpr std::uint8_t kFlashFrames = 3;

    // Paint a band of rows flat before anything sits on top of it. Both text
    // bands do this, and they must: a glyph writes only its lit pixels, so
    // without the wash underneath, the unlit ones would still be showing the
    // frame from two draws ago.
    template <class Canvas>
    static void fill_rows(Canvas& c, int top, int rows, Ink ink) noexcept {
        for (int y = top; y < top + rows; ++y) { c.hline(0, y, kPanelWidth, ink); }
    }

    template <class Canvas>
    void draw_header(const DisplaySnapshot& snap, Canvas& c) noexcept {
        fill_rows(c, kHeaderTop, kHeaderRows, Ink::Background);

        // The value slot carries the ONE thing worth 27 pixels of header: the
        // last price while the book is live, and why it is not while it is not.
        // A stale panel showing a price it cannot vouch for and no reason would
        // be the wrong half of the payload on screen.
        const TextField last = TextField::scaled(snap.last_px, snap.symbol.price_decimals);
        const char* value = snap.live() ? (snap.has_last ? last.buf : "-")
                                        : reason_text(snap.stale_reason);
        const int value_w = text_width(value);
        int value_x = kPanelWidth - value_w;
        if (value_x < 0) { value_x = 0; }
        draw_text(c, value_x, kHeaderTop, value, Ink::Value);

        // THE AGE, AND IT IS PROVISIONAL — M4 stage D, A4.
        //
        // `age_ms` has existed on DisplaySnapshot since stage A2 and no panel has
        // ever drawn it, because the firmware never stamped it. It does now, and
        // the bench needs to be able to read it: a book's queuing lag is the one
        // thing on this object that a person watching the ladder cannot infer
        // from the ladder.
        //
        // WHERE IT GOES IS PART B'S DECISION, NOT THIS ONE. The brief says so —
        // "the value slot currently carries last-price and stale-reason; age is a
        // third claimant on the same pixels" — and Part A is told to take no
        // rendering decisions. So this takes the cheapest placement that answers
        // A4 and pre-empts nothing: the age sits to the LEFT of the value, in the
        // slot the symbol already occupies, under the same yield rule the symbol
        // already had. Nothing moves. `kLevels` stays 26, every row constant in
        // test_ladder_render.cpp keeps its value, and a Part B that decides the
        // age belongs somewhere else deletes eight lines rather than unpicking a
        // second header row (which would cost THREE levels a side, not one).
        //
        // THE PRIORITY IS VALUE > AGE > SYMBOL, and it is the existing rule
        // extended by one step rather than a new one. The value has always won
        // outright — a stale panel showing a price it cannot vouch for and no
        // reason is the wrong half of the payload on screen. The symbol has
        // always yielded, because this object shows exactly one of them and a
        // header that overlaps is worse than one missing an id nobody is reading.
        // The age lands between: more useful than the id, never at the value's
        // expense.
        //
        // `-` when there is no reading, which is a different statement from 0.0s
        // and is `AgeText::unknown()`'s whole reason for existing. It is what the
        // panel shows for the first 16 s of an Anvil connection and 32 s of a
        // Kraken one, while the baseline latches.
        const AgeText age = snap.has_age ? AgeText(snap.age_ms) : AgeText::unknown();
        const int age_w = text_width(age.buf);
        int left_limit = value_x;
        if (age_w + kGlyphAdvance <= left_limit) {
            const int age_x = left_limit - kGlyphAdvance - age_w;
            draw_text(c, age_x, kHeaderTop, age.buf, Ink::Symbol);
            left_limit = age_x;
        }

        // The symbol is the anchor, but it yields, and it now yields to two
        // things instead of one.
        const TextField sym = TextField::scaled(static_cast<std::int64_t>(snap.symbol.id), 0);
        const int sym_w = text_width(sym.buf);
        if (sym_w + kGlyphAdvance <= left_limit) {
            draw_text(c, 0, kHeaderTop, sym.buf, Ink::Symbol);
        }
    }

    // levels[0] is the touch and sits at touch_row, hard against the spread;
    // successive levels walk away from it in `step`, so the worst level on each
    // side lands at the outside of the panel and the two best are adjacent in the
    // middle, where the eye is. Rows the book does not fill are still painted —
    // every row of the frame is written every frame, because the back buffer
    // holds a frame from two draws ago.
    template <class Canvas>
    void draw_side(Canvas& c, const SidePlan& side, Qty max_qty) noexcept {
        const int n = side.count < kLevels ? side.count : kLevels;
        for (int i = 0; i < kLevels; ++i) {
            const int row = side.touch_row + i * side.step;
            if (i >= n) {
                c.hline(0, row, kPanelWidth, Ink::Background);
                continue;
            }
            draw_level(c, row, side.levels[i], max_qty, i == 0 ? side.best : side.rest);
        }
    }

    template <class Canvas>
    void draw_level(Canvas& c, int row, const BookLevel& lvl, Qty max_qty, Ink ink) noexcept {
        const int len = bar_length(lvl.qty, max_qty);
        const Ink fill = (flash_frames_ > 0 && lvl.px == flash_px_) ? Ink::Flash : ink;
        if (len > 0) { c.hline(0, row, len, fill); }
        if (len < kPanelWidth) { c.hline(len, row, kPanelWidth - len, Ink::Background); }
    }

    template <class Canvas>
    void draw_strip(Canvas& c) noexcept {
        fill_rows(c, kStripTop, kStripRows, Ink::Background);

        // Two rows is a tape, not a chart: each column says whether that sample
        // was in the upper or lower half of the window's range. It is enough to
        // see the last price moving, which is the whole job of the strip, and it
        // costs no rows the ladder wanted.
        if (spark_count_ >= 2) {
            PriceTicks lo = spark_at(0);
            PriceTicks hi = lo;
            for (int j = 1; j < spark_count_; ++j) {
                const PriceTicks v = spark_at(j);
                if (v < lo) { lo = v; }
                if (v > hi) { hi = v; }
            }
            const int first_x = kSparkColumns - spark_count_;
            for (int j = 0; j < spark_count_; ++j) {
                const PriceTicks v = spark_at(j);
                int row = kStripTop + kStripRows - 1;
                if (hi > lo) {
                    const std::int64_t up = (static_cast<std::int64_t>(v - lo) * (kStripRows - 1)) /
                                            static_cast<std::int64_t>(hi - lo);
                    row = kStripTop + (kStripRows - 1) - static_cast<int>(up);
                }
                c.pixel(first_x + j, row, Ink::Tape);
            }
        }

        c.pixel(kBeatX, kBeatY, beat_ ? Ink::Beat : Ink::Background);
    }

    // Oldest sample first. The ring is written newest-at-head, so index 0 is
    // head - count.
    PriceTicks spark_at(int j) const noexcept {
        const int idx = (spark_head_ + kSparkColumns - spark_count_ + j) % kSparkColumns;
        return spark_[idx];
    }

    // Clipped per COLUMN, not per glyph. A caller that hands this a string wider
    // than the panel — a price at a scale nobody planned for, say — would
    // otherwise have its last glyph's third column land at x == 64 and rely on
    // the canvas to throw it away. The panel's driver does bounds-check, but
    // "the renderer stays inside the frame" is a property worth holding here,
    // where the host test's guard region can see it, rather than downstream in a
    // library.
    template <class Canvas>
    static void draw_text(Canvas& c, int x, int y, const char* s, Ink ink) noexcept {
        if (s == nullptr) { return; }
        for (int n = 0; s[n] != '\0'; ++n) {
            const int gx = x + n * kGlyphAdvance;
            if (gx >= kPanelWidth) { return; }
            for (int r = 0; r < kGlyphHeight; ++r) {
                const int py = y + r;
                if (py < 0 || py >= kPanelHeight) { continue; }
                const GlyphRow bits = glyph_row(s[n], r);
                if (bits == 0) { continue; }
                for (int col = 0; col < kGlyphWidth; ++col) {
                    const int px = gx + col;
                    if (px < 0 || px >= kPanelWidth) { continue; }
                    if ((bits >> (kGlyphWidth - 1 - col)) & 1u) {
                        c.pixel(px, py, ink);
                    }
                }
            }
        }
    }

    PriceTicks spark_[kSparkColumns]{};
    std::uint8_t spark_head_ = 0;
    std::uint8_t spark_count_ = 0;

    PriceTicks flash_px_ = 0;
    std::uint8_t flash_frames_ = 0;
    Seq last_trade_seq_ = 0;

    std::uint32_t last_version_ = 0;
    bool have_seen_ = false;
    bool beat_ = false;
};

static_assert(kSparkColumns <= 255, "spark_count_/spark_head_ are uint8_t");

}  // namespace depthcharge::fw
