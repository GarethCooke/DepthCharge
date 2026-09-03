// depthcharge/book.hpp — the phase-1 book engine.
//
// ARCHITECTURE §5, "Phase 1 degenerate form (Anvil-only, M1–M3): snapshots-only
// venues need no book maintenance at all — 'adopt latest snapshot' *is* the
// engine, plus a trade ring. Build that first; do not gold-plate ahead of the
// first delta venue."
//
// M4 STAGE B1 ADDS THE ONE THING THAT WAS MISSING, AND IT IS NOT THE DENSE
// WINDOW. Kraken sends deltas, so `apply(Delta)` had to stop being a refusal —
// but the tick-indexed dense window is stage C, and this is deliberately not it.
// A Delta lands in the SAME sorted arrays a Snapshot already fills, by ordered
// insert / amend / erase: O(depth) per level, over a ladder whose depth is 25
// and whose measured traffic is 1.55 levels per message. That is the phase-1
// shape doing one more thing, not a new engine.
//
// WHAT STAGE C ACTUALLY CHANGED, AND THE ONE THING IT DID NOT.
//
// The paragraph this replaces predicted that stage C would swap the STORAGE for
// ARCHITECTURE §5's tick-indexed dense window — a contiguous array of Qty
// addressed by (px - anchor), plus a cold tail — and leave apply()/publish()
// alone. **That is not what stage C was asked for and it is not what it did.**
// C's brief is about which levels of a deeper-than-the-panel book earn one of
// the 27 rendered rows, which is a question about `publish` and not about
// storage. So `publish` now runs a selectable window policy (window.hpp) and the
// arrays below are exactly what they were.
//
// The storage half is therefore still unbuilt, and it is unbuilt on a
// measurement rather than by omission: `amend_side` is an ordered insert over a
// side of 25, at a measured 1.55 changed levels per message and ~26 messages a
// second — on the order of a thousand 16-byte shifts a second. A tick-indexed
// store would remove work that has never been shown to cost anything. Recorded
// in ARCHITECTURE §9 rather than left as a silent divergence from §5, because
// §5 still promises it and somebody will come looking.
//
// Invariants this file is answerable for:
//   #3  Integer ticks only — there is not a float in the type.
//   #5  Stale is a state of the book, not of the renderer. Any Gap marks it and
//       only a Snapshot clears it.
//   #7  Fixed-capacity storage, no allocation, ever — construction included.
//   #8  One writer: apply() is the feed side. publish() fills a caller-owned
//       DisplaySnapshot and touches nothing else.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "depthcharge/display_snapshot.hpp"
#include "depthcharge/feed_event.hpp"
#include "depthcharge/ladder.hpp"
#include "depthcharge/symbol.hpp"
#include "depthcharge/window.hpp"

namespace depthcharge {

// The book retains exactly what an adapter may hand it (feed_event.hpp), so a
// Snapshot the adapter accepted can never be truncated a second time here.
// 2 x 256 x 16 B = 8 KiB — internal SRAM on the ESP32-S3, plain heap on the
// host. The render window (kDisplayLevels) is far smaller; the extra depth is
// what a later zoom mode (M7) reads without changing this class.
inline constexpr std::size_t kBookCapacity = kMaxSnapshotLevels;

class Book {
public:
    struct Stats {
        std::uint64_t snapshots_adopted = 0;
        std::uint64_t trades_applied = 0;
        std::uint64_t gaps = 0;
        std::uint64_t deltas_applied = 0;    // amended or inserted a level
        std::uint64_t deltas_removed = 0;    // of which qty == 0 removals
        std::uint64_t deltas_absent = 0;     // removal for a level not held
        std::uint64_t deltas_overflowed = 0; // a side already at kBookCapacity
        std::uint64_t publishes = 0;

        // THE CROSSED-TOUCH GUARD (M5, 2026-09-01). A published frame whose best
        // bid sits at or above its best ask is an invariant-grade wrong output:
        // the panel is claiming a book that cannot exist. Counted rather than
        // asserted, and counted on the PUBLISHED window rather than the internal
        // ladder, so window selection is inside the check and not beside it.
        //
        // WHY THIS DID NOT EXIST UNTIL A SOAK FOUND IT. M5 stage B1's often-quoted
        // "0 crossed, 0 touched across all seven captures" was asked of the
        // VENUE's own published books -- the REST bodies and the @depth20
        // partials -- and not of the book this class maintains
        // (NOTES-binance.md, deliverable 6). It is a true statement about the
        // venue and it never covered our arithmetic. Nothing anywhere asserted
        // this, so every golden in the suite would have graded a crossed book
        // green.
        //
        // Two integer comparisons on the publish path: no float near book data,
        // no allocation, no new FeedEvent kind. It does not branch rendering on
        // rendered state (invariant #5) -- it counts, and the renderer never
        // reads it.
        std::uint64_t crossed_publishes = 0;
        PriceTicks    worst_cross_ticks = 0;   // most negative spread published
    };

    // `policy` defaults to the firmware's compile-time choice, so the board and
    // every existing call site get the same window without naming it; the
    // harness passes one explicitly, because a golden for each policy has to be
    // a test rather than a rebuild.
    //
    // `validated_depth` is how many of this venue's BEST levels an external
    // check confirms — Kraken's CRC32 covers 10, Anvil has none and passes 0.
    // Caller-supplied configuration exactly as `symbol` is: it never crosses the
    // adapter boundary as data (invariant #2), and this class does not know what
    // a checksum is.
    explicit Book(const SymbolSpec& symbol,
                  window::Policy policy = window::kWindowPolicy,
                  std::uint32_t validated_depth = 0) noexcept
        : symbol_(symbol), policy_(policy), validated_depth_(validated_depth) {}

    const SymbolSpec& symbol() const noexcept { return symbol_; }
    const Stats& stats() const noexcept { return stats_; }
    FeedStatus status() const noexcept { return status_; }
    GapReason stale_reason() const noexcept { return stale_reason_; }
    Seq last_seq() const noexcept { return last_seq_; }
    std::uint32_t bid_count() const noexcept { return bid_count_; }
    std::uint32_t ask_count() const noexcept { return ask_count_; }

    // HAS THIS BOOK EVER BEEN TOLD ANYTHING? (M4 stage C.)
    //
    // Distinct from empty, and the distinction is knowledge: a book that has
    // received nothing and a book whose side is genuinely empty both report
    // `bid_count() == 0`, and only the second is a statement the venue made.
    // Monotonic — it becomes true at the first Snapshot and never goes back,
    // because a Gap makes the book UNKNOWN rather than un-received, and
    // `mark_stale` deliberately keeps the levels for the renderer to grey.
    //
    // Note the deliberate asymmetry with the Kraken adapter, which DOES throw
    // its ladder away on a gap (kraken_adapter.hpp, THE HEALING PATH): the
    // adapter must not amend a book whose provenance is a hole, while the panel
    // must not blank a ladder the feed never retracted. Two different jobs, two
    // different answers, and B2's healing path is what made both reachable in
    // one run.
    bool initialised() const noexcept { return initialised_; }

    window::Policy window_policy() const noexcept { return policy_; }
    const window::WindowStats& bid_window() const noexcept { return bid_window_; }
    const window::WindowStats& ask_window() const noexcept { return ask_window_; }

    // The feed side. Bounded work, no allocation, no I/O.
    void apply(const FeedEvent& ev) noexcept {
        last_seq_ = ev.seq;
        switch (ev.kind) {
            case FeedEvent::Kind::Snapshot: adopt(ev); break;
            case FeedEvent::Kind::Trade:    record_trade(ev); break;
            case FeedEvent::Kind::Gap:      mark_stale(ev.reason); break;
            case FeedEvent::Kind::Delta:    amend(ev); break;
        }
    }

    // Fill a caller-owned snapshot with the top of the book. Bumps `version` so
    // the render side can tell frames apart. Not const: the version stamp is
    // producer state.
    void publish(DisplaySnapshot& out) noexcept {
        ++version_;
        ++stats_.publishes;

        out.version = version_;
        out.symbol = symbol_;
        out.seq = last_seq_;
        out.status = status_;
        out.stale_reason = stale_reason_;
        out.has_last = has_last_;
        out.last_px = last_px_;

        // THE WINDOW, built here rather than read back and patched: one writer,
        // one pass, into storage the snapshot already owns (invariant #8, and
        // stage C brief §1).
        out.initialised = initialised_;
        bid_window_ = window::select(policy_, bids_, bid_count_, out.bids,
                                     kDisplayLevels, validated_depth_);
        ask_window_ = window::select(policy_, asks_, ask_count_, out.asks,
                                     kDisplayLevels, validated_depth_);
        out.bid_count = static_cast<std::uint8_t>(bid_window_.rows_filled);
        out.ask_count = static_cast<std::uint8_t>(ask_window_.rows_filled);
        // The rows the window did not fill are UNKNOWN, and they are blanked so
        // a shallower frame cannot leave the previous one's tail behind it —
        // `bid_count` is what says where the knowledge stops, and the zero-fill
        // is what stops a stale level being mistaken for a live one.
        std::fill(out.bids + bid_window_.rows_filled, out.bids + kDisplayLevels, BookLevel{});
        std::fill(out.asks + ask_window_.rows_filled, out.asks + kDisplayLevels, BookLevel{});

        // Ring -> newest-first array. trade_head_ is the *next* write slot, so
        // the newest print sits one behind it, modulo the ring. The walk stays a
        // hand loop: kTradeRingSize is a power of two, so `% ring` is a single
        // AND, and expressing it as two std::reverse_copy calls measured bigger
        // and slower on both the host and the ESP32-S3.
        const std::uint32_t ring = static_cast<std::uint32_t>(kTradeRingSize);
        const std::uint32_t n = trade_count_;
        for (std::uint32_t i = 0; i < n; ++i) {
            out.trades[i] = trades_[(trade_head_ + ring - 1 - i) % ring];
        }
        std::fill(out.trades + n, out.trades + ring, TradePrint{});
        out.trade_count = static_cast<std::uint8_t>(n);

        // ...and the one thing nobody was checking. See Stats::crossed_publishes.
        if (out.bid_count != 0 && out.ask_count != 0 &&
            out.bids[0].px >= out.asks[0].px) {
            ++stats_.crossed_publishes;
            const PriceTicks spread = out.asks[0].px - out.bids[0].px;
            if (spread < stats_.worst_cross_ticks) { stats_.worst_cross_ticks = spread; }
        }
    }

private:
    // A Snapshot *replaces*; it never merges. Levels beyond kBookCapacity cannot
    // occur (the adapter caps at the same constant), but the clamp is kept so a
    // future adapter bug truncates instead of writing off the end.
    void adopt(const FeedEvent& ev) noexcept {
        bid_count_ = copy_clamped<kBookCapacity>(ev.bids.data, ev.bids.size, bids_);
        ask_count_ = copy_clamped<kBookCapacity>(ev.asks.data, ev.asks.size, asks_);
        status_ = FeedStatus::Live;
        initialised_ = true;
        ++stats_.snapshots_adopted;
    }

    void record_trade(const FeedEvent& ev) noexcept {
        // A print that arrives while stale is still a real print — it is tape,
        // not book state — so it is recorded, but it does NOT clear the stale
        // flag: only a fresh Snapshot can (invariant #5).
        const std::uint32_t ring = static_cast<std::uint32_t>(kTradeRingSize);
        trades_[trade_head_] = TradePrint{ev.px, ev.qty, ev.seq, ev.side};
        trade_head_ = (trade_head_ + 1) % ring;
        if (trade_count_ < ring) { ++trade_count_; }
        last_px_ = ev.px;
        has_last_ = true;
        ++stats_.trades_applied;
    }

    void mark_stale(GapReason reason) noexcept {
        // Levels are deliberately kept. A gap means the book is *unknown*, not
        // empty, and a blank panel would say something the feed never said; the
        // renderer greys what is there instead. Only a Snapshot clears it.
        status_ = FeedStatus::Stale;
        stale_reason_ = reason;
        ++stats_.gaps;
    }

    // One level, absolute quantity, `qty == 0` removes (ARCHITECTURE §4).
    //
    // A DELTA DOES NOT CLEAR STALE, AND THAT IS THE INVARIANT-5 HALF OF THIS
    // FUNCTION. Only a Snapshot re-baselines the book. A book that is stale is
    // one whose contents are UNKNOWN, and amending an unknown book produces
    // another unknown book — so deltas keep arriving and being applied (the
    // adapter is the one that decides whether they are worth applying at all),
    // and the panel stays grey until something replaces the whole thing.
    //
    // The venue's adapter is responsible for not sending nonsense here: Kraken's
    // drops updates that arrive before its first snapshot, because deltas onto
    // an empty ladder reproduce 0 of 49 of the venue's checksums. This function
    // is the mechanical half and makes no such judgement.
    void amend(const FeedEvent& ev) noexcept {
        if (ev.side == Side::Bid) {
            amend_side(bids_, bid_count_, ev.px, ev.qty, Side::Bid);
        } else {
            amend_side(asks_, ask_count_, ev.px, ev.qty, Side::Ask);
        }
    }

    // Ordered insert / amend / erase over the same sorted array a Snapshot
    // fills. The ordering and the array shifting are `depthcharge::ladder`'s —
    // SHARED WITH THE ADAPTER'S LADDER, because the engine's book is a mirror
    // built entirely from what the adapter emits, and two private definitions of
    // "better" that disagreed would draw a book the adapter never held.
    void amend_side(BookLevel* side, std::uint32_t& count, PriceTicks px, Qty qty,
                    Side s) noexcept {
        const std::uint32_t at = ladder::rank_of(side, count, px, s);
        const bool found = ladder::holds(side, count, at, px);

        if (qty == 0) {
            if (!found) {
                // A removal for a level this book does not hold. Counted, not
                // treated as a gap: at a truncating venue the level legitimately
                // fell out of the subscribed depth before we ever saw it, and
                // greying for that would be a lie about a book that is right.
                ++stats_.deltas_absent;
                return;
            }
            ladder::erase_at(side, count, at);
            ++stats_.deltas_applied;
            ++stats_.deltas_removed;
            return;
        }

        if (found) {
            side[at].qty = qty;
            ++stats_.deltas_applied;
            return;
        }

        if (at >= kBookCapacity) {
            ++stats_.deltas_overflowed;
            return;
        }
        BookLevel displaced{};
        if (ladder::insert_at(side, count, at, BookLevel{px, qty}, kBookCapacity,
                              displaced)) {
            // Cannot happen through an adapter that truncates to a subscribed
            // depth at or below this capacity, and counted rather than asserted
            // for exactly that reason: if it ever fires, the number says so
            // instead of the book silently dropping its worst level per message.
            ++stats_.deltas_overflowed;
        }
        ++stats_.deltas_applied;
    }

    // Copy at most Cap levels into the book's own store.
    //
    // IT USED TO HAVE A SECOND HALF AND STAGE C TOOK THE CALLER AWAY. Until the
    // window landed this was `copy_clamped<Cap, ZeroFill>`, shared between the
    // book's store (256 deep, never blanked) and the display window (27 deep,
    // always blanked), with the parameters templated so neither the fill
    // decision nor the capacity reached a runtime branch on adopt()'s path. The
    // display half is now `window::select` plus an explicit fill in publish(),
    // so `ZeroFill` had exactly no callers left — and a template parameter kept
    // alive by a comment describing a use that no longer exists is worse than
    // the duplication it was extracted to remove.
    //
    // The measurement that justified the consolidation is kept because it is
    // still the reason `Cap` is a template parameter: at -Os over the engine's
    // hot path (apply + publish + parse/format + channel, forced out of line)
    // the shared form cost xtensa GCC 8.4 1494 -> 1541 B and GCC 15.2
    // 1500 -> 1520 B. Both forms lower to memcpy; neither pulls in a library
    // call the old loops did not.
    template <std::size_t Cap>
    static std::uint32_t copy_clamped(const BookLevel* src, std::uint32_t count,
                                      BookLevel* dst) noexcept {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(count, Cap));
        // A Snapshot may legally carry no levels at all, and an empty span's
        // pointer may be null — which std::copy_n must not be handed even for a
        // zero count.
        if (n != 0) { std::copy_n(src, n, dst); }
        return n;
    }

    SymbolSpec symbol_{};
    window::Policy policy_ = window::kWindowPolicy;
    std::uint32_t validated_depth_ = 0;
    window::WindowStats bid_window_{};
    window::WindowStats ask_window_{};

    BookLevel bids_[kBookCapacity]{};
    BookLevel asks_[kBookCapacity]{};
    std::uint32_t bid_count_ = 0;
    std::uint32_t ask_count_ = 0;

    TradePrint trades_[kTradeRingSize]{};
    std::uint32_t trade_head_ = 0;
    std::uint32_t trade_count_ = 0;

    PriceTicks last_px_ = 0;
    bool has_last_ = false;

    // Stale until proven otherwise: the book starts having adopted nothing, and
    // "no data yet" must not be drawable as live. Resync = awaiting the first
    // Snapshot.
    FeedStatus status_ = FeedStatus::Stale;
    GapReason stale_reason_ = GapReason::Resync;
    bool initialised_ = false;

    Seq last_seq_ = 0;
    std::uint32_t version_ = 0;
    Stats stats_{};
};

}  // namespace depthcharge
