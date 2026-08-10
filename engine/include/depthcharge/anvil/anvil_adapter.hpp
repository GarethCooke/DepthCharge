// depthcharge/anvil/anvil_adapter.hpp — Anvil frames -> FeedEvents.
//
// The first VenueAdapter. It owns every Anvil-specific decision so the book
// never sees one (invariant #2):
//
//   * `snapshot` and `book` are the same thing on this wire — a full top-N
//     replace (protocol §3.1/§3.2, confirmed byte-identical at M0) — so both
//     become FeedEvent::Snapshot. `book` is not a delta and must never be
//     treated as one.
//   * `trade` becomes FeedEvent::Trade with `aggr` mapped to the aggressor side.
//   * `summary` is cross-ticker roster data for the M7 board mode. Tolerated,
//     counted, ignored: it is not ladder input.
//   * Seq is SYNTHESISED from receive order. Anvil's wire seq is one global
//     counter across every ticker and frame type, so a single socket's
//     subsequence is sparse and non-monotonic (M0: 42 backward steps / 5 min,
//     and it does not reset across a reconnect). This adapter therefore never
//     emits Gap{SeqGap} — there is no sequence to gap-test. That is safe here
//     and only here, because Anvil's book frames are idempotent full replaces:
//     a lost frame costs one 80 ms refresh, not book integrity. Kraken and
//     Binance will do the opposite, which is why this is the adapter's job and
//     not the engine's (ARCHITECTURE §4).
//   * Gap{Disconnect} comes from the transport, not the wire — Anvil emits no
//     error or gap frame at all. The caller (host replay driver at M1, the
//     firmware net task at M3) calls on_transport_gap().
//
// The sink is a template so there is no virtual call and no allocation on the
// feed path (invariant #7); it is any callable taking `const FeedEvent&`.
#pragma once

#include <cstdint>
#include <string_view>

#include "depthcharge/anvil/anvil_frame.hpp"
#include "depthcharge/feed_event.hpp"
#include "depthcharge/symbol.hpp"

namespace depthcharge::anvil {

// Ticker 101 as deployed on anvil.garethcooke.com. price_decimals = 4 is a
// DepthCharge-side declaration, not a wire fact: the protocol carries no tick
// metadata (M1 known unknown). It is the measured maximum precision Anvil emits
// — every price in both committed traces and both 5-minute local captures has
// <= 4 decimals — and the adapter rejects anything finer rather than rounding
// it, so a server that started quoting 5 decimals would fail loudly on the
// first frame instead of drawing a subtly wrong ladder.
inline constexpr SymbolSpec kAnvilTicker101{/*id=*/101, /*price_decimals=*/4, /*qty_step=*/1};
static_assert(kAnvilTicker101.valid(), "a declared SymbolSpec must be decodable");

class AnvilAdapter {
public:
    struct Stats {
        std::uint64_t frames_in = 0;
        std::uint64_t events_out = 0;
        std::uint64_t snapshot_frames = 0;   // wire "snapshot"
        std::uint64_t book_frames = 0;       // wire "book"
        std::uint64_t trade_frames = 0;
        std::uint64_t summary_ignored = 0;
        std::uint64_t other_ticker = 0;
        std::uint64_t unknown_kind = 0;
        std::uint64_t parse_errors = 0;      // malformed JSON / shape
        std::uint64_t price_errors = 0;      // scale disagreement (BadPrice)
        std::uint64_t truncated_frames = 0;  // deeper than kMaxSnapshotLevels
        std::uint64_t transport_gaps = 0;
        std::uint64_t wire_seq_backward = 0; // the M0 finding, observed live
    };

    explicit AnvilAdapter(const SymbolSpec& symbol) noexcept : symbol_(symbol) {}

    const SymbolSpec& symbol() const noexcept { return symbol_; }
    const Stats& stats() const noexcept { return stats_; }
    Seq next_seq() const noexcept { return next_seq_; }

    // The last wire `seq` seen, for DIAGNOSTICS ONLY — it is already tracked for
    // `wire_seq_backward` and this exposes it rather than adding state.
    //
    // Read the warning in the header comment before using it for anything: this
    // is Anvil's global counter, shared across every ticker and frame type, so
    // it is not this stream's sequence and nothing may order, dedupe or gap-test
    // on it. What it is good for is a CLOCK — its rate is Anvil's whole-engine
    // publish rate, so comparing the step across a silence against that rate
    // says whether the frame that ended the silence is fresh (the middle was
    // never sent to us) or stale (it was sent and arrived late). That is the M3
    // stall characterisation's shed-versus-delayed discriminator, and it is the
    // only reason this accessor exists.
    std::int64_t last_wire_seq() const noexcept { return last_wire_seq_; }
    bool have_wire_seq() const noexcept { return have_wire_seq_; }

    // One received WebSocket text frame, verbatim. Emits 0 or 1 FeedEvents.
    //
    // A frame that will not parse is counted and dropped, NOT turned into a Gap:
    // Anvil republishes the whole book every ~80 ms, so a dropped frame
    // self-heals within one refresh, and greying the panel for it would be a lie
    // in the other direction. A *systematic* failure shows up as a stalled
    // ladder plus a non-zero parse_errors count in the replay report.
    template <typename Sink>
    void on_frame(std::string_view json, Sink&& sink) {
        ++stats_.frames_in;
        // frame_ is not reset here: parse_anvil_frame owns that, on entry and on
        // every non-Ok exit (anvil_frame.hpp). Two owners meant two places for a
        // future implementation of the seam to get it wrong.
        const ParseStatus st = parse_anvil_frame(json, symbol_, frame_);
        if (st != ParseStatus::Ok) {
            switch (st) {
                case ParseStatus::BadPrice:    ++stats_.price_errors; break;
                case ParseStatus::OtherTicker: ++stats_.other_ticker; break;
                default:                       ++stats_.parse_errors; break;
            }
            return;
        }

        // Diagnostics only — see the header comment on why this can never gate
        // an event.
        if (frame_.has_wire_seq) {
            if (have_wire_seq_ && frame_.wire_seq < last_wire_seq_) {
                ++stats_.wire_seq_backward;
            }
            last_wire_seq_ = frame_.wire_seq;
            have_wire_seq_ = true;
        }

        if (frame_.levels_truncated) { ++stats_.truncated_frames; }

        switch (frame_.kind) {
            case FrameKind::Snapshot:
                ++stats_.snapshot_frames;
                emit_snapshot(sink);
                break;
            case FrameKind::Book:
                ++stats_.book_frames;
                emit_snapshot(sink);
                break;
            case FrameKind::Trade:
                ++stats_.trade_frames;
                emit_trade(sink);
                break;
            case FrameKind::Summary:
                ++stats_.summary_ignored;
                break;
            case FrameKind::Unknown:
                ++stats_.unknown_kind;
                break;
        }
    }

    // The transport says the stream broke: socket close, RX watchdog expiry,
    // or a receive ring overflow. This is the only source of Gap on this venue.
    template <typename Sink>
    void on_transport_gap(GapReason reason, Sink&& sink) {
        ++stats_.transport_gaps;
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Gap;
        ev.reason = reason;
        emit(ev, sink);
    }

private:
    // Monotonic, dense, one per emitted event — the property ARCHITECTURE §4
    // requires and the wire does not give us.
    Seq take_seq() noexcept { return next_seq_++; }

    // The one door every event leaves by: stamp the synthesised Seq, count it,
    // hand it to the sink. Three copies of that tail meant the density
    // guarantee lived in three places; now it can be checked by reading one.
    template <typename Sink>
    void emit(FeedEvent& ev, Sink& sink) {
        ev.seq = take_seq();
        ++stats_.events_out;
        sink(ev);
    }

    template <typename Sink>
    void emit_snapshot(Sink& sink) {
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Snapshot;
        ev.bids = LevelSpan{frame_.bids, frame_.bid_count};
        ev.asks = LevelSpan{frame_.asks, frame_.ask_count};
        // The spans point into frame_, which stays valid for exactly this call —
        // the lifetime rule in feed_event.hpp. The parser's reset on the next
        // frame is what ends it.
        emit(ev, sink);
    }

    template <typename Sink>
    void emit_trade(Sink& sink) {
        FeedEvent ev{};
        ev.kind = FeedEvent::Kind::Trade;
        ev.px = frame_.trade_px;
        ev.qty = frame_.trade_qty;
        ev.side = frame_.aggressor;
        emit(ev, sink);
    }

    SymbolSpec symbol_{};
    AnvilFrame frame_{};       // reused staging buffer; ~8 KiB, never reallocated
    Seq next_seq_ = 1;         // 0 is left free to mean "no event yet"
    std::int64_t last_wire_seq_ = 0;
    bool have_wire_seq_ = false;
    Stats stats_{};
};

}  // namespace depthcharge::anvil
