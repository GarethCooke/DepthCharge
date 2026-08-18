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
// What stage C changes, so the boundary is legible from here: the dense window
// replaces the STORAGE (a contiguous array of Qty addressed by px - anchor, plus
// a cold tail) and leaves this apply()/publish() face alone. Nothing below
// anticipates it, and nothing below should be reshaped in order to.
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
    };

    explicit Book(const SymbolSpec& symbol) noexcept : symbol_(symbol) {}

    const SymbolSpec& symbol() const noexcept { return symbol_; }
    const Stats& stats() const noexcept { return stats_; }
    FeedStatus status() const noexcept { return status_; }
    GapReason stale_reason() const noexcept { return stale_reason_; }
    Seq last_seq() const noexcept { return last_seq_; }
    std::uint32_t bid_count() const noexcept { return bid_count_; }
    std::uint32_t ask_count() const noexcept { return ask_count_; }

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

        out.bid_count = static_cast<std::uint8_t>(
            copy_clamped<kDisplayLevels, true>(bids_, bid_count_, out.bids));
        out.ask_count = static_cast<std::uint8_t>(
            copy_clamped<kDisplayLevels, true>(asks_, ask_count_, out.asks));

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
    }

private:
    // A Snapshot *replaces*; it never merges. Levels beyond kBookCapacity cannot
    // occur (the adapter caps at the same constant), but the clamp is kept so a
    // future adapter bug truncates instead of writing off the end.
    void adopt(const FeedEvent& ev) noexcept {
        bid_count_ = copy_clamped<kBookCapacity, false>(ev.bids.data, ev.bids.size, bids_);
        ask_count_ = copy_clamped<kBookCapacity, false>(ev.asks.data, ev.asks.size, asks_);
        status_ = FeedStatus::Live;
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

    // Copy at most Cap levels; when ZeroFill, blank the rest of the destination
    // so a shallower book cannot leave the previous frame's tail behind it.
    //
    // Cap and ZeroFill are template parameters, not arguments, on purpose: the
    // book's own store (256 deep, never filled) and the display window (27 deep,
    // always filled) are the same operation but must not become the same *code*.
    // A runtime fill flag would put the decision on adopt()'s path 12 times a
    // second, and a runtime capacity would cost the compiler the constant it
    // needs to lower each instantiation separately.
    //
    // Measured cost of the consolidation on the engine's hot path at -Os
    // (Book::apply + publish + parse/format + channel, forced out of line):
    // xtensa GCC 8.4 1494 -> 1541 B, GCC 15.2 1500 -> 1520 B. Both forms lower
    // to memcpy/memset; neither introduces a library call the old loops did not
    // already pull in. 47 bytes against 16 MB of flash, for one helper instead
    // of two near-identical ones.
    template <std::size_t Cap, bool ZeroFill>
    static std::uint32_t copy_clamped(const BookLevel* src, std::uint32_t count,
                                      BookLevel* dst) noexcept {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(count, Cap));
        // A Snapshot may legally carry no levels at all, and an empty span's
        // pointer may be null — which std::copy_n must not be handed even for a
        // zero count.
        if (n != 0) { std::copy_n(src, n, dst); }
        if constexpr (ZeroFill) { std::fill(dst + n, dst + Cap, BookLevel{}); }
        return n;
    }

    SymbolSpec symbol_{};

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

    Seq last_seq_ = 0;
    std::uint32_t version_ = 0;
    Stats stats_{};
};

}  // namespace depthcharge
