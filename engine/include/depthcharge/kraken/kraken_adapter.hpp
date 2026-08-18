// depthcharge/kraken/kraken_adapter.hpp — Kraken v2 frames -> FeedEvents.
//
// The second VenueAdapter, and the first one that has to MAINTAIN A BOOK. Anvil
// republishes a full top-N every ~80 ms, so its adapter is a pure function of
// one frame; Kraken sends an opening snapshot and then deltas for ever, so the
// venue's book has to live somewhere. It lives here, and that is the decision
// this file is mostly about — see THE LADDER below.
//
// Every Kraken-specific decision is owned here so the book never sees one
// (invariant #2):
//
//   * `book/snapshot` becomes FeedEvent::Snapshot (a full replace).
//     `book/update` becomes one FeedEvent::Delta PER CHANGED LEVEL, with
//     absolute quantity and `qty == 0` meaning removed — which is §4's Delta
//     semantics verbatim, and is why no new FeedEvent variant is needed.
//   * Seq is SYNTHESISED from emission order. This wire carries no sequence, no
//     offset and no message count — confirmed by inspection of every frame kind
//     — so there is nothing to gap-test on. ARCHITECTURE §4 already said so.
//   * The CHECKSUM is parsed and carried, NOT verified (B1 scope). Verification,
//     and the Gap{ChecksumFail} it raises, are B2. Carrying it now is what makes
//     B2 add a check rather than a parse.
//   * Gap{Disconnect} comes from the transport, not the wire — with one wire
//     exception this venue introduces, the refused subscribe. See THE ACK.
//
// ============================================================================
// THE LADDER, AND WHY THE ADAPTER TRUNCATES RATHER THAN THE BOOK
// ============================================================================
//
// Kraken sends NO removal for a level that merely falls out of the subscribed
// depth: the client truncates its own book. That reads like housekeeping and it
// is not. Stage 0 measured a non-truncating client against the venue's own
// CRC32 and it is wrong 1,077 times in 1,537 messages at depth 25 — the extra
// levels the venue has stopped mentioning rank inside our best 10 while they do
// not rank inside the venue's, so the top of the ladder on the panel is simply
// not the top of the venue's book.
//
// So somebody must hold a depth-limited book. Two places were available:
//
//   (a) the ENGINE's book learns a subscribed depth and truncates;
//   (b) the ADAPTER holds the venue's book, truncates it, and emits the
//       resulting changes as Deltas.
//
// (b), and the deciding argument is B2 rather than taste. The checksum is
// computed over the top 10 of the TRUNCATED book, so whoever truncates is
// whoever can verify. Under (a) the adapter would have to read the book back to
// checksum it — inverting the one-way adapter -> engine flow that invariant #2
// exists to protect, and putting a venue's CRC rule inside `engine/`. Under (b)
// the verification is local, the book stays venue-free, and a level evicted by
// truncation leaves as an ordinary Delta{qty = 0}, which is a thing the §4
// vocabulary already says.
//
// The cost, stated rather than discovered: the venue's book and the display book
// are two copies. The ladder below is 2 x kMaxSnapshotLevels x 16 B = 8 KiB, on
// top of KrakenFrame's 8 KiB, in an adapter that is one per build (ARCHITECTURE
// §9, 2026-08-17: one venue per build until M7). It is sized at
// kMaxSnapshotLevels rather than at the subscribed depth because `depth` is a
// RUNTIME parameter here — the harness replays committed slices at 10, 25 and
// 100 in one binary, and a compile-time depth would mean a golden that could
// only be run by rebuilding.
//
// ============================================================================
// THE ACK, INCLUDING THE ABSENCE CASE
// ============================================================================
//
// Three states, and the important one is the one you start in.
//
//   Unknown     no ack seen. **Book messages are accepted.**
//   Subscribed  an ack with success:true.
//   Refused     an ack with success:false or an `error`. Terminal.
//
// **ABSENCE OF A SUBSCRIBE IS NOT FAILURE OF A SUBSCRIBE**, and this is the trap
// that would otherwise make the one trace that matters unusable. The extreme
// quiet-pair slice (kraken_minagbp_d25_20260817) begins MID-STREAM: its first
// record is a heartbeat, there is no `status`, no subscribe and no ack anywhere
// in the file. An adapter that required an ack before accepting data would emit
// nothing for that trace — and that trace is the one carrying the 25,843 ms
// healthy book silence the whole 2026-08-17 staleness ruling rests on. The rule
// would fail exactly where the evidence for it lives.
//
// A REFUSED subscribe is the opposite, and it is a fatal transport error rather
// than a new GapReason (ARCHITECTURE §9, 2026-08-17, M4 stage A — the question
// has now been asked three times and answered no twice). Stage 0 measured why it
// has to be loud: after `{"error":"Subscription depth not supported",...,
// "success":false}` the connection STAYS UP, `status` has already arrived, and
// heartbeats keep coming at 1 Hz — so a client that did not check `success`
// sits on a live, chattering socket with a permanently empty book and nothing
// that looks like an error after the first second. That is invariant #5's
// forbidden output arriving through the one door it does not watch.
//
// This file cannot call `die()` — it is `engine/`, and invariant #1 keeps it
// free of ESP-IDF. So it does the portable half: it raises Gap{Disconnect}, so
// the panel greys immediately, and latches `refused()`, which the firmware's
// feed task turns into `die()` and the harness reports. The policy is the
// caller's; the honesty is not optional and is therefore here.
//
// ============================================================================
// NO BASELINE, NO DELTAS
// ============================================================================
//
// A delta is an amendment to a book you have. Before the first snapshot there is
// no book to amend, and applying deltas to an empty one does not approximate the
// venue's book — it produces a different book that merely looks plausible.
// Measured on the mid-stream slice: replaying its 49 updates onto an empty
// ladder reproduces **0 of 49** of the venue's checksums, while the four slices
// that open with a snapshot reproduce **4,878 of 4,878**.
//
// So updates before the first snapshot are counted (`deltas_before_baseline`)
// and dropped. The book stays Stale{Resync} — which is the true statement, since
// nothing has baselined it — and the heartbeat still stamps the liveness clock,
// so the trace remains exactly as usable for the staleness ruling as it was.
// This is the difference between an honest grey and a confident wrong ladder,
// and it is the same choice invariant #5 makes everywhere else.
#pragma once

#include <cstdint>
#include <string_view>

#include "depthcharge/feed_event.hpp"
#include "depthcharge/kraken/kraken_frame.hpp"
#include "depthcharge/ladder.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::kraken {

// ---------------------------------------------------------------------------
// THE DEPTH TIERS — A WHITELIST, NOT A ROUNDING
// ---------------------------------------------------------------------------

// Kraken's offered ladder. Two venues, one parameter, opposite behaviours:
// Anvil's `depth` is a request that is silently rounded UP to the next tier
// (M3 A7), Kraken's is a strict enum that is REFUSED — measured, with depth 27:
// `{"error":"Subscription depth not supported","success":false}`. So the general
// rule A7 left behind (a venue's depth parameter is a request, not a contract)
// survives, and an adapter must handle both failure modes and assume neither.
inline constexpr std::int32_t kOfferedDepths[] = {10, 25, 100, 500, 1000};

constexpr bool is_offered_depth(std::int32_t d) noexcept {
    for (const std::int32_t o : kOfferedDepths) {
        if (o == d) { return true; }
    }
    return false;
}

// The depth the FIRMWARE subscribes at. A compile-time constant because one
// venue per build is a firmware decision (ARCHITECTURE §9, 2026-08-17), and
// static_asserted against the offered ladder because 27 — the number that would
// match kDisplayLevels exactly, and the number this project asked Anvil for —
// is REFUSED here, silently and permanently.
//
// 25 rather than 100, and the argument is not bytes: the checksum covers the top
// 10 levels only, so a delta lost at level 40 of a depth-100 subscription is
// invisible to it and stays wrong until the next reconnect.
inline constexpr std::int32_t kKrakenSubscribeDepth = 25;
static_assert(is_offered_depth(kKrakenSubscribeDepth),
              "Kraken's depth is a strict enum and an unsupported value is REFUSED, "
              "not rounded up: the subscribe returns success:false and the socket "
              "then stays up, heartbeating, over a permanently empty book");
static_assert(!is_offered_depth(27),
              "27 is the panel's kDisplayLevels and is deliberately NOT offered — "
              "this assertion is here so a future reader who reaches for it finds "
              "the reason rather than the refusal");

// THE TWO DEEP TIERS DO NOT FIT, AND THAT IS A LIMIT RATHER THAN A BUG — but it
// has to be a LOUD limit, because the failure mode is silent.
//
// `kMaxSnapshotLevels` is 256 (feed_event.hpp), and Kraken offers 500 and 1000.
// A subscription at either would be clamped to 256 by `clamp_depth` below and
// the book would simply be short — and the CRC32 would NOT catch it, because it
// covers only the top 10 levels. So a depth-500 client would run indefinitely
// with a quietly truncated book and a clean checksum on every message.
//
// Nothing subscribes that deep today: the firmware's constant is 25 and stage 0
// never tested 500 or 1000 (recorded as a known unknown in NOTES-kraken.md). The
// static_assert is what makes a future session that raises the constant meet the
// question at compile time instead of at the panel; the runtime clamp is
// reported through `depth()` so a harness reading a deep capture can see what it
// actually got rather than what the file asked for.
static_assert(kKrakenSubscribeDepth <= static_cast<std::int32_t>(kMaxSnapshotLevels),
              "the subscribed depth must fit the adapter's staging buffer and ladder; "
              "Kraken's 500 and 1000 tiers exceed kMaxSnapshotLevels (256) and would "
              "be silently truncated — the top-10 CRC32 cannot detect that");

// The two symbols the committed slices carry, declared the way ARCHITECTURE §4
// requires and then VERIFIED per price by the parser.
//
// §4's declare-and-verify rule becomes something better at this venue: Anvil
// publishes no tick metadata so DepthCharge must declare it, but Kraken
// publishes it on the same socket (the v2 `instrument` channel: `price_precision`
// 1, `qty_precision` 8 for BTC/USD). These declarations were measured against it
// and agree exactly, over 8,172 level entries in five slices. Reading the
// channel at runtime costs one extra subscription and is deliberately NOT done
// at B1 — its numbers arrive in exponent notation (`1e-08`, `5e-05`), which is a
// second number grammar for one field this build already knows.
//
// `id` is a DepthCharge-side integer, not a venue id: §4 keeps the engine
// integer-only, so the string pair stops at the adapter (kraken_frame.hpp).
inline constexpr SymbolConfig kKrakenBtcUsd{
    SymbolSpec{/*id=*/1, /*price_decimals=*/1, /*qty_decimals=*/8}, "BTC/USD"};
inline constexpr SymbolConfig kKrakenMinaGbp{
    SymbolSpec{/*id=*/2, /*price_decimals=*/4, /*qty_decimals=*/8}, "MINA/GBP"};
static_assert(kKrakenBtcUsd.spec.valid(), "a declared SymbolSpec must be decodable");
static_assert(kKrakenMinaGbp.spec.valid(), "a declared SymbolSpec must be decodable");

// Look a wire symbol up in the declared set. Returns false for a symbol this
// build has no scale for, which must be a loud refusal rather than a guessed
// scale: a wrong scale does not fail, it draws a wrong ladder.
constexpr bool symbol_config_for(std::string_view wire_symbol, SymbolConfig& out) noexcept {
    if (wire_symbol == kKrakenBtcUsd.wire_symbol) { out = kKrakenBtcUsd; return true; }
    if (wire_symbol == kKrakenMinaGbp.wire_symbol) { out = kKrakenMinaGbp; return true; }
    return false;
}

// ---------------------------------------------------------------------------

class KrakenAdapter {
public:
    enum class SubscribeState : std::uint8_t { Unknown, Subscribed, Refused };

    struct Stats {
        std::uint64_t frames_in = 0;
        std::uint64_t events_out = 0;
        std::uint64_t snapshot_frames = 0;
        std::uint64_t update_frames = 0;
        std::uint64_t heartbeats = 0;       // the liveness signal
        std::uint64_t status_frames = 0;
        std::uint64_t acks = 0;
        std::uint64_t unknown_kind = 0;
        std::uint64_t parse_errors = 0;     // NotJson / BadShape
        std::uint64_t price_errors = 0;     // BadPrice — a scale disagreement
        std::uint64_t qty_errors = 0;       // BadQty
        std::uint64_t other_symbol = 0;     // a book message for someone else's pair
        std::uint64_t transport_gaps = 0;

        // Level bookkeeping — the numbers that say whether the ladder is doing
        // its job, and the ones B2's CRC will be read against.
        std::uint64_t levels_applied = 0;      // deltas emitted from wire levels
        std::uint64_t levels_removed = 0;      // of which qty == 0 removals
        std::uint64_t levels_evicted = 0;      // pushed out by TRUNCATION, not by the venue
        std::uint64_t levels_outside_depth = 0;// arrived already worse than our worst
        std::uint64_t levels_unchanged = 0;    // re-sent at the quantity we hold
        std::uint64_t deltas_before_baseline = 0;  // dropped: no snapshot yet
        // A SNAPSHOT deeper than we subscribed. Zero at Kraken today, which
        // serves exactly the depth requested — but Anvil rounds depth UP, and
        // this adapter is required to assume neither behaviour.
        std::uint64_t levels_deeper_than_subscribed = 0;

        std::uint64_t checksums_seen = 0;   // carried, never verified at B1
        std::uint64_t ack_depth_mismatch = 0;
    };

    KrakenAdapter(const SymbolConfig& cfg, std::int32_t depth) noexcept
        : cfg_(cfg), depth_(clamp_depth(depth)) {}

    const SymbolSpec& symbol() const noexcept { return cfg_.spec; }
    std::string_view wire_symbol() const noexcept { return cfg_.wire_symbol; }
    std::int32_t depth() const noexcept { return depth_; }
    const Stats& stats() const noexcept { return stats_; }
    Seq next_seq() const noexcept { return next_seq_; }

    SubscribeState subscribe_state() const noexcept { return sub_; }
    // Latched, terminal, and the firmware's cue to die() so the supervisor
    // retries — a live socket over an empty book is the failure this catches.
    bool refused() const noexcept { return sub_ == SubscribeState::Refused; }
    // Has a snapshot baselined the ladder? Until it has, updates are dropped.
    bool has_baseline() const noexcept { return baselined_; }

    // The last checksum the venue sent, CARRIED AND NOT VERIFIED. B2 turns this
    // into a comparison against the ladder's own CRC32; B1's job is to make sure
    // it is already here when B2 arrives.
    std::uint32_t last_checksum() const noexcept { return last_checksum_; }
    bool has_checksum() const noexcept { return has_checksum_; }

    ParseStatus last_status() const noexcept { return last_status_; }

    std::uint32_t bid_count() const noexcept { return bid_count_; }
    std::uint32_t ask_count() const noexcept { return ask_count_; }

    // One received WebSocket text frame, verbatim. Emits 0..N FeedEvents: a
    // snapshot is one, an update is one per CHANGED level plus one per level
    // truncation evicted.
    template <typename Sink>
    void on_frame(std::string_view json, Sink&& sink) {
        ++stats_.frames_in;
        const ParseStatus st = parse_kraken_frame(json, cfg_, frame_);
        last_status_ = st;
        if (st != ParseStatus::Ok) {
            switch (st) {
                case ParseStatus::BadPrice:    ++stats_.price_errors; break;
                case ParseStatus::BadQty:      ++stats_.qty_errors; break;
                case ParseStatus::OtherSymbol: ++stats_.other_symbol; break;
                default:                       ++stats_.parse_errors; break;
            }
            return;
        }

        if (frame_.has_checksum) {
            last_checksum_ = frame_.checksum;
            has_checksum_ = true;
            ++stats_.checksums_seen;
        }

        switch (frame_.kind) {
            case FrameKind::Heartbeat:    ++stats_.heartbeats; break;
            case FrameKind::Status:       ++stats_.status_frames; break;
            case FrameKind::SubscribeAck: on_ack(sink); break;
            case FrameKind::BookSnapshot: ++stats_.snapshot_frames; adopt_snapshot(sink); break;
            case FrameKind::BookUpdate:   ++stats_.update_frames; apply_update(sink); break;
            case FrameKind::Unknown:      ++stats_.unknown_kind; break;
        }
    }

    // The transport says the stream broke: socket close, liveness watchdog
    // expiry, or a receive ring overflow.
    //
    // The ladder dies with the socket, and that is not housekeeping either: a
    // reconnect is served a fresh snapshot, and deltas applied across a gap onto
    // a pre-gap ladder are the same "different book that looks plausible" as
    // deltas applied before any baseline. Dropping the baseline here is what
    // makes the resync path (B2) start from nothing rather than from a book
    // whose provenance is a hole.
    template <typename Sink>
    void on_transport_gap(GapReason reason, Sink&& sink) {
        ++stats_.transport_gaps;
        bid_count_ = 0;
        ask_count_ = 0;
        baselined_ = false;
        has_checksum_ = false;
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Gap;
        ev.reason = reason;
        emit(ev, sink);
    }

private:
    static constexpr std::uint32_t clamp_depth(std::int32_t d) noexcept {
        if (d <= 0) { return static_cast<std::uint32_t>(kKrakenSubscribeDepth); }
        return static_cast<std::uint32_t>(d) > kMaxSnapshotLevels
                   ? static_cast<std::uint32_t>(kMaxSnapshotLevels)
                   : static_cast<std::uint32_t>(d);
    }

    Seq take_seq() noexcept { return next_seq_++; }

    // The one door every event leaves by — same shape as the Anvil adapter's,
    // for the same reason: the Seq density guarantee lives in one place.
    template <typename Sink>
    void emit(FeedEvent& ev, Sink& sink) {
        ev.seq = take_seq();
        ++stats_.events_out;
        sink(ev);
    }

    template <typename Sink>
    void emit_delta(PriceTicks px, Qty qty, Side side, Sink& sink) {
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Delta;
        ev.px = px;
        ev.qty = qty;
        ev.side = side;
        emit(ev, sink);
    }

    template <typename Sink>
    void on_ack(Sink& sink) {
        ++stats_.acks;
        if (!frame_.ack_success || frame_.ack_has_error) {
            // Terminal. The panel greys now rather than after a timeout, because
            // nothing else on this connection will ever say anything is wrong.
            sub_ = SubscribeState::Refused;
            FeedEvent ev{};
            ev.kind = FeedEvent::Kind::Gap;
            ev.reason = GapReason::Disconnect;
            emit(ev, sink);
            return;
        }
        sub_ = SubscribeState::Subscribed;
        // The served depth is reported back in the ack. Kraken serves exactly
        // what was asked for (10/25/100 each measured at 10/10, 25/25, 100/100),
        // so a mismatch is a wire change rather than a rounding — counted, and
        // the ADAPTER'S depth wins, because that is the one the ladder truncates
        // at and a silent disagreement between the two is a wrong top of book.
        if (frame_.ack_depth >= 0 &&
            static_cast<std::uint32_t>(frame_.ack_depth) != depth_) {
            ++stats_.ack_depth_mismatch;
        }
    }

    // A snapshot REPLACES, **AND IT TRUNCATES TO THE SUBSCRIBED DEPTH LIKE
    // EVERYTHING ELSE.**
    //
    // The first version of this seeded every level the frame carried, capped
    // only at the staging buffer's 256. That was wrong, and no committed slice
    // could show it: Kraken serves EXACTLY the depth requested (measured 10/10,
    // 25/25, 100/100), so the frame never carried more than `depth_` and the two
    // caps coincided on all five files. It reproduced on a synthetic frame in
    // one line — a 5-level snapshot against a subscribed depth of 2 left the
    // ladder holding 5 — and the consequence is the exact defect stage 0
    // measured as fatal: a non-truncating book, wrong in 1,077 of 1,537 messages
    // at depth 25, because it keeps levels the venue has stopped mentioning.
    //
    // Two live routes reach it, so it is not hypothetical. A capture whose
    // metadata records no `depth` falls back to the firmware's constant while
    // the file may be a depth-100 slice; and `ack_depth_mismatch` exists
    // precisely because a venue may serve a depth other than the one asked for —
    // Anvil already rounds UP, and NOTES-kraken.md says this adapter "must handle
    // both failure modes and may assume neither".
    //
    // Levels are rebuilt through the same ordered insert the update path uses
    // rather than copied in wire order. The venue sends them best-first and every
    // captured frame does, but "the venue sorted them for us" is an assumption a
    // ladder should not need, and one path means one definition of sorted.
    template <typename Sink>
    void adopt_snapshot(Sink& sink) {
        bid_count_ = 0;
        ask_count_ = 0;
        for (std::uint32_t i = 0; i < frame_.bid_count; ++i) {
            seed(bids_, bid_count_, frame_.bids[i], Side::Bid);
        }
        for (std::uint32_t i = 0; i < frame_.ask_count; ++i) {
            seed(asks_, ask_count_, frame_.asks[i], Side::Ask);
        }
        baselined_ = true;

        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Snapshot;
        ev.bids = LevelSpan{bids_, bid_count_};
        ev.asks = LevelSpan{asks_, ask_count_};
        // The spans point into this adapter's ladder, which outlives the sink
        // call — a stronger guarantee than feed_event.hpp's lifetime rule
        // requires, and deliberately not relied upon by anything.
        emit(ev, sink);
    }

    // One snapshot level into the ladder. No event and no eviction report: a
    // snapshot is a full replace, so nothing it displaces was ever published.
    void seed(BookLevel* side, std::uint32_t& count, const BookLevel& lvl, Side s) noexcept {
        if (lvl.qty <= 0) { return; }   // a snapshot carrying a removal
        const std::uint32_t at = ladder::rank_of(side, count, lvl.px, s);
        if (ladder::holds(side, count, at, lvl.px)) {
            side[at].qty = lvl.qty;     // a price duplicated within one snapshot
            return;
        }
        if (at >= depth_) {
            // Deeper than we subscribed. Counted, because a venue serving more
            // than it was asked for is a wire fact worth seeing rather than a
            // silent drop.
            ++stats_.levels_deeper_than_subscribed;
            return;
        }
        BookLevel displaced{};
        if (ladder::insert_at(side, count, at, lvl, depth_, displaced)) {
            ++stats_.levels_deeper_than_subscribed;
        }
    }

    template <typename Sink>
    void apply_update(Sink& sink) {
        if (!baselined_) {
            // No book to amend. Counted rather than silently ignored, because
            // "the trace started mid-stream" and "we are dropping data" must be
            // distinguishable in a report.
            stats_.deltas_before_baseline +=
                static_cast<std::uint64_t>(frame_.bid_count) + frame_.ask_count;
            return;
        }
        for (std::uint32_t i = 0; i < frame_.bid_count; ++i) {
            apply_level(bids_, bid_count_, frame_.bids[i], Side::Bid, sink);
        }
        for (std::uint32_t i = 0; i < frame_.ask_count; ++i) {
            apply_level(asks_, ask_count_, frame_.asks[i], Side::Ask, sink);
        }
    }

    // One wire level onto the ladder. Emits what actually changed, and nothing
    // that did not. Ordering and array shifting belong to depthcharge::ladder,
    // shared with the engine's book so the mirror cannot sort differently from
    // the original.
    template <typename Sink>
    void apply_level(BookLevel* side, std::uint32_t& count, const BookLevel& lvl,
                     Side s, Sink& sink) {
        const std::uint32_t at = ladder::rank_of(side, count, lvl.px, s);
        const bool found = ladder::holds(side, count, at, lvl.px);

        if (lvl.qty == 0) {
            if (!found) {
                // A removal for a level we never held: it fell outside our
                // depth before it was ever sent to us. Not an error, and NOT
                // emitted — the engine's book mirrors this ladder, so a Delta
                // for a level neither of them holds would be an event with no
                // state change behind it.
                ++stats_.levels_outside_depth;
                return;
            }
            ladder::erase_at(side, count, at);
            ++stats_.levels_applied;
            ++stats_.levels_removed;
            emit_delta(lvl.px, 0, s, sink);
            return;
        }

        if (found) {
            if (side[at].qty == lvl.qty) {
                ++stats_.levels_unchanged;
                return;
            }
            side[at].qty = lvl.qty;
            ++stats_.levels_applied;
            emit_delta(lvl.px, lvl.qty, s, sink);
            return;
        }

        if (at >= depth_) {
            // Worse than the worst level we are subscribed to. The venue is
            // telling us about a level outside our window; holding it would be
            // the non-truncating defect that fails 1,077 of 1,537 checksums.
            ++stats_.levels_outside_depth;
            return;
        }

        BookLevel evicted{};
        const bool displaced = ladder::insert_at(side, count, at, lvl, depth_, evicted);
        ++stats_.levels_applied;
        emit_delta(lvl.px, lvl.qty, s, sink);

        if (displaced) {
            // THE EVICTION IS A REAL EVENT AND NOT BOOKKEEPING. Kraken sends no
            // removal for it — that is the whole truncation rule — so if the
            // adapter does not say it, the engine's book keeps a level the
            // venue has stopped mentioning and the top of the ladder drifts.
            // Emitted AFTER the insert so the pair reads in the order it
            // happened: a level arrived and pushed one out.
            ++stats_.levels_evicted;
            emit_delta(evicted.px, 0, s, sink);
        }
    }

    SymbolConfig cfg_{};
    std::uint32_t depth_ = 25;
    KrakenFrame frame_{};      // reused staging buffer; ~8 KiB, never reallocated

    // The venue's book, truncated to `depth_`. Best-first: bids descending,
    // asks ascending.
    BookLevel bids_[kMaxSnapshotLevels]{};
    BookLevel asks_[kMaxSnapshotLevels]{};
    std::uint32_t bid_count_ = 0;
    std::uint32_t ask_count_ = 0;

    Seq next_seq_ = 1;         // 0 is left free to mean "no event yet"
    SubscribeState sub_ = SubscribeState::Unknown;  // absence is not failure
    bool baselined_ = false;
    std::uint32_t last_checksum_ = 0;
    bool has_checksum_ = false;
    ParseStatus last_status_ = ParseStatus::Ok;
    Stats stats_{};
};

}  // namespace depthcharge::kraken
