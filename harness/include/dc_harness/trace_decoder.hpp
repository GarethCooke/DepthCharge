// dc_harness/trace_decoder.hpp — the per-venue half of reading a trace.
//
// M4 stage A, deliverable 2. The reader (trace.hpp) owns the ENVELOPE: a line
// is a JSON object with an integer rx_ns, an object `frame`, and a monotonic
// clock. It hands on the VERBATIM frame text and knows nothing else. Everything
// past that is dialect, and dialect lives here, one decoder per venue.
//
// TWO THINGS A DECODER DOES, and they are deliberately separate:
//
//   classify(record)      names the record's kind and says whether it reaches
//                         the book. Cheap, no engine involvement, and what the
//                         statistics pass and the taxonomy pins are built on.
//   decode(record, sink)  turns the record into FeedEvents. THIS is the seam
//                         invariant #2 guards, and the one a second adapter has
//                         to fit.
//
// **Kraken's decoder was a classifier at stage A and is an ADAPTER from B1.**
// The shape stage A froze is the shape B1 filled: `decode()` kept its sink
// parameter while emitting nothing, precisely so that dropping the adapter in
// would not reshape the seam. It did not — the signature below is unchanged and
// the body is now `adapter_.on_frame(...)`, which is the same line Anvil's
// decoder has had since M1.
//
// CLASSIFY AND DECODE STAY SEPARATE, AND B1 IS WHERE THAT EARNS ITS KEEP.
// `classify()` is a trace-reading concern (what kind of record is this, does it
// stamp the liveness clock) and runs in tools that hold no adapter at all;
// `decode()` is the invariant-#2 seam. They now disagree about one thing on
// purpose: classify calls a `book/update` a book event because it IS one on the
// wire, while decode may emit nothing for it — an update before the first
// snapshot is dropped by the adapter (kraken_adapter.hpp). A single combined
// entry point could not express that without lying to one of its two callers.
//
// WHY static_assertS AND NOT A `concept`: ARCHITECTURE §9, 2026-08-16 (stage 0),
// strain 3 → option (ii). The C++20 concept *syntax* compiles on the xtensa GCC
// 8.4 target for one flag (`-fconcepts`), but that flag selects the Concepts TS,
// not C++20 concepts — so host and target would compile one spelling under two
// dialects, which type-checks and is therefore a worse failure than convention.
// The cost of option (ii) is stated in that row and is real: these fire INSIDE
// the template, so a diagnostic names this file's line rather than the caller's.
// This is the first place the decision applies.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include <depthcharge/anvil/anvil_adapter.hpp>
#include <depthcharge/feed_event.hpp>
#include <depthcharge/kraken/kraken_adapter.hpp>
#include <depthcharge/symbol.hpp>

#include "dc_harness/trace.hpp"
#include "dc_harness/venue.hpp"

namespace dc::harness {

// What a decoder says about one record. `name` is borrowed from the decoder and
// is valid until its next classify() call — the same lifetime rule
// TraceFrame::frame_json already has, for the same reason (no allocation per
// record on a 2,500-record trace).
//
// There is deliberately no `is_resync` here. Whether a snapshot is a RESYNC is
// a property of the trace, not of the record: it depends on what came before
// it. The reader answers it, venue-independently, in accumulate() — see the
// RESYNC RULE there. Two earlier drafts of this file put it in the decoder and
// got it wrong at Kraken twice, in opposite directions.
struct RecordKind {
    std::string_view name;
    bool is_snapshot = false;    // a full replace: re-baselines the book
    bool is_book_event = false;  // reaches the book — the AGE clock
    // The venue's declared liveness signal (venue.hpp): Anvil's `summary`,
    // Kraken's `heartbeat`. This is what invariant #5's grey is armed on since
    // the 2026-08-17 ruling — book silence is now age, and age is a number in
    // the header rather than a colour.
    bool is_liveness = false;
};

// ---------------------------------------------------------------------------
// THE CONTRACTS
// ---------------------------------------------------------------------------

// The sink a decoder emits into, used to state the contract without naming a
// real one. It is the LEAST a sink can be, so anything that satisfies the
// assertions against it satisfies them against a fatter sink too.
struct ContractProbeSink {
    void operator()(const depthcharge::FeedEvent&) const {}
};

// A FeedEvent sink: callable with (const FeedEvent&), returning void.
//
// Instantiate this inside a decoder's decode() so it fires on the sink type the
// CALLER actually passed — which is where the mistake is made. That is the one
// thing option (ii) still buys at the call site.
//
// `noexcept` is deliberately NOT asserted, and the reason is not an oversight.
// The stage-0 write-up floated it, but the only real sink in the tree —
// replay_driver.cpp's — pushes onto a std::vector and calls a std::function
// observer, both of which can throw. Asserting noexcept would either reject the
// live path or turn a throw into std::terminate on the desk. The firmware sink
// is a different object with different obligations; if it wants noexcept, it
// says so there.
template <typename Sink>
struct SinkContract {
    static_assert(std::is_invocable_v<Sink&, const depthcharge::FeedEvent&>,
                  "a FeedEvent sink must be callable as sink(const FeedEvent&)");
    static_assert(
        std::is_void_v<std::invoke_result_t<Sink&, const depthcharge::FeedEvent&>>,
        "a FeedEvent sink must return void — a return value here would be a "
        "decision the adapter cannot act on (ARCHITECTURE §4: Gap is data, and "
        "back-pressure is invariant #4's problem, not the sink's)");
    static constexpr bool ok = true;
};

// Detection traits for the two member templates. Written as one trait per
// requirement with the return type folded in, so a decoder that is missing a
// member and a decoder whose member returns the wrong thing both fail the SAME
// assertion with the same message, instead of the second one hard-erroring past
// a failed first.
template <typename D, typename = void>
struct dc_decodes_into_sink : std::false_type {};
template <typename D>
struct dc_decodes_into_sink<
    D, std::enable_if_t<std::is_void_v<decltype(std::declval<D&>().decode(
           std::declval<const TraceFrame&>(), std::declval<ContractProbeSink&>()))>>>
    : std::true_type {};

template <typename D, typename = void>
struct dc_reports_transport_gap : std::false_type {};
template <typename D>
struct dc_reports_transport_gap<
    D, std::enable_if_t<std::is_void_v<decltype(std::declval<D&>().on_transport_gap(
           depthcharge::GapReason::Disconnect, std::declval<ContractProbeSink&>()))>>>
    : std::true_type {};

template <typename D, typename = void>
struct dc_classifies : std::false_type {};
template <typename D>
struct dc_classifies<D, std::enable_if_t<std::is_same_v<
                            RecordKind, decltype(std::declval<D&>().classify(
                                            std::declval<const TraceFrame&>()))>>>
    : std::true_type {};

// A venue decoder. Instantiated once per decoder below, so adding a third venue
// fails the build here rather than diverging quietly — which is exactly what
// DESIGN strain 3 has been warning about since M1 and what the second adapter
// makes real.
template <typename Decoder>
struct DecoderContract {
    static_assert(std::is_same_v<std::remove_cv_t<decltype(Decoder::kVenue)>, Venue>,
                  "a venue decoder must declare `static constexpr Venue kVenue` — "
                  "the tag it is dispatched on, and, since B1, the identity that "
                  "travels with everything it produces (see `name()`)");
    static_assert(dc_classifies<Decoder>::value,
                  "a venue decoder must provide RecordKind classify(const TraceFrame&)");
    static_assert(dc_decodes_into_sink<Decoder>::value,
                  "a venue decoder must provide a member template "
                  "`void decode(const TraceFrame&, Sink&)`; a decoder that emits "
                  "nothing still takes the sink, because its signature is the one "
                  "the venue's adapter fills in later");
    static_assert(dc_reports_transport_gap<Decoder>::value,
                  "a venue decoder must provide a member template "
                  "`void on_transport_gap(GapReason, Sink&)` — Gap{Disconnect} is "
                  "synthesised transport-side at every venue (ARCHITECTURE §4)");
    static constexpr bool ok = true;
};

// ---------------------------------------------------------------------------
// ANVIL — the live path, unchanged in behaviour
// ---------------------------------------------------------------------------

// Anvil's classification, as a free function because two callers need it — the
// decoder below and RecordClassifier at the bottom of this file — and a second
// copy of a rule is how the two adapters drift (DESIGN strain 3, which this
// file exists to close rather than to demonstrate).
//
// Anvil puts its kind in `type` and the reader has already decoded it, so this
// is a view onto the reader's buffer and costs nothing, and the classification
// is stateless.
inline RecordKind anvil_classify(const TraceFrame& f) noexcept {
    RecordKind k;
    k.name = f.type;
    k.is_snapshot = f.type == "snapshot";
    // What reaches the book: full replaces and trades. `summary` is cross-ticker
    // roster data the adapter tolerates and ignores (anvil_adapter.hpp), so it
    // is not a book event and must not stamp invariant #5's clock.
    k.is_book_event = k.is_snapshot || f.type == "book" || f.type == "trade";
    // `summary` is cross-ticker roster data the adapter tolerates and ignores,
    // so it is NOT a book event — and that is exactly what makes it usable as
    // the liveness signal: it proves the server is alive without pretending the
    // book moved. Measured a fixed 2 Hz deadline on a completely empty queue.
    k.is_liveness = f.type == "summary";
    return k;
}

// A thin, non-owning shape over AnvilAdapter. It adds classification (which the
// adapter has no business knowing about — it is a trace-reading concern) and
// otherwise forwards. Deliberately thin: "Anvil's path is unchanged in
// behaviour and in output" is a stage-A constraint, proven by diffing every
// committed trace through the pre- and post-change readers.
class AnvilTraceDecoder {
public:
    static constexpr Venue kVenue = Venue::Anvil;

    explicit AnvilTraceDecoder(const depthcharge::SymbolSpec& symbol) noexcept
        : adapter_(symbol) {}

    depthcharge::anvil::AnvilAdapter& adapter() noexcept { return adapter_; }
    const depthcharge::anvil::AnvilAdapter& adapter() const noexcept { return adapter_; }

    // WHICH DECODER PRODUCED THIS. Derived from kVenue rather than spelled as a
    // second literal, so it cannot drift from the tag it is dispatched on —
    // swapping the dispatch changes this string, which is the whole point of it
    // existing (M4 triage item 1).
    static constexpr std::string_view name() noexcept {
        return venue_traits(kVenue).name;
    }

    RecordKind classify(const TraceFrame& f) const noexcept { return anvil_classify(f); }

    template <typename Sink>
    void decode(const TraceFrame& f, Sink&& sink) {
        static_assert(SinkContract<std::remove_reference_t<Sink>>::ok);
        adapter_.on_frame(f.frame_json, sink);
    }

    template <typename Sink>
    void on_transport_gap(depthcharge::GapReason reason, Sink&& sink) {
        static_assert(SinkContract<std::remove_reference_t<Sink>>::ok);
        adapter_.on_transport_gap(reason, sink);
    }

private:
    depthcharge::anvil::AnvilAdapter adapter_;
};

// ---------------------------------------------------------------------------
// KRAKEN — the adapter (M4 stage B1)
// ---------------------------------------------------------------------------

// Naming a Kraken record's kind. A FREE FUNCTION, exactly as anvil_classify is,
// and for the same reason: two callers need it — the decoder below and
// RecordClassifier at the foot of this file — and a second copy of a rule is how
// two adapters drift (DESIGN strain 3).
//
// It was a member of the decoder until B1. It could not stay one: the decoder
// now owns a KrakenAdapter, which needs a symbol and a depth, and `dc_taxonomy`
// counts records in files whose symbol this build may have no scale for. A tool
// that only counts records must not have to construct an 8 KiB staging buffer
// and declare a SymbolSpec it has no use for.
//
// `scratch` backs the returned `name` until the caller's next call — the same
// lifetime rule TraceFrame::frame_json already has, for the same reason (no
// allocation per record on a 2,500-record trace).
//
// The kind names are the SAME STRINGS tools/kraken_frame_economics.py's
// frame_kind() produces — `book/update`, `heartbeat`, `ack:subscribe`,
// `ack:subscribe REFUSED` — plus `tx:subscribe` for a record this side sent,
// which the Python tool skips rather than names. Two languages measuring one
// committed file should agree on what they are counting, and stage 0 already
// paid for the version of this where they did not.
//
// `ack:subscribe REFUSED` is not cosmetic. A failed subscribe carries `method`
// and `success:false`, so filing it as an ordinary ack hides the exact trap
// stage 0 found: a live socket, 1 Hz heartbeats, and a permanently empty book.
RecordKind kraken_classify(const TraceFrame& f, std::string& scratch);

// Kraken frames -> FeedEvents, through the real adapter.
//
// The adapter needs two things the trace metadata carries and the Anvil path has
// no equivalent of: the WIRE SYMBOL (a string pair, not an integer ticker) and
// the SUBSCRIBED DEPTH (the ladder truncates at it, and the committed slices run
// at 10, 25 and 100 in one binary).
//
// `wire_symbol` IS BORROWED, AND IT MUST OUTLIVE THIS OBJECT. In practice it is
// always a view into the venue table's `inline constexpr` literals
// (kraken_adapter.hpp), which have static storage duration and cannot dangle.
//
// **The first draft of this class owned the string instead, and that was a
// use-after-move rather than a style preference.** A `std::string symbol_` member
// with the adapter's SymbolConfig viewing it looks self-contained and is not:
// `Replay` takes its decoder by value, so the decoder gets MOVED, and a moved
// std::string short enough for SSO copies its bytes into the new object's inline
// storage — leaving the view pointing into the moved-from husk. It compiled, it
// passed the test that constructed a decoder directly, and it failed only
// through `run_replay`, which is the one path that moves. Borrowing a static
// literal removes the hazard rather than documenting it.
class KrakenTraceDecoder {
public:
    static constexpr Venue kVenue = Venue::Kraken;

    KrakenTraceDecoder(const depthcharge::SymbolSpec& spec, std::string_view wire_symbol,
                       std::int32_t depth)
        : adapter_(depthcharge::kraken::SymbolConfig{spec, wire_symbol}, depth) {}

    depthcharge::kraken::KrakenAdapter& adapter() noexcept { return adapter_; }
    const depthcharge::kraken::KrakenAdapter& adapter() const noexcept { return adapter_; }

    // See AnvilTraceDecoder::name(). Derived from kVenue, never a second literal.
    static constexpr std::string_view name() noexcept {
        return venue_traits(kVenue).name;
    }

    RecordKind classify(const TraceFrame& f) { return kraken_classify(f, kind_); }

    template <typename Sink>
    void decode(const TraceFrame& f, Sink&& sink) {
        static_assert(SinkContract<std::remove_reference_t<Sink>>::ok);
        // A record this side SENT is not the venue speaking. Feeding our own
        // subscribe to the adapter would file it as an Unknown frame and put a
        // record in a counter that is supposed to describe the venue.
        if (f.is_tx) { return; }
        adapter_.on_frame(f.frame_json, sink);
    }

    template <typename Sink>
    void on_transport_gap(depthcharge::GapReason reason, Sink&& sink) {
        static_assert(SinkContract<std::remove_reference_t<Sink>>::ok);
        adapter_.on_transport_gap(reason, sink);
    }

private:
    depthcharge::kraken::KrakenAdapter adapter_;
    std::string kind_;    // backs RecordKind::name until the next classify()
};

static_assert(DecoderContract<AnvilTraceDecoder>::ok);
static_assert(DecoderContract<KrakenTraceDecoder>::ok);

// ---------------------------------------------------------------------------
// RUNTIME DISPATCH
// ---------------------------------------------------------------------------

// Classification for a caller that holds a trace rather than a venue — the
// statistics pass in trace.cpp, dc_replay and dc_taxonomy. Holds the per-venue
// classifier state and switches on the tag once per record.
//
// It carries no adapter: classification is the half of a decoder that costs
// nothing, and a tool that only counts records should not be constructing an
// 8 KiB staging buffer or declaring a SymbolSpec it has no use for.
class RecordClassifier {
public:
    explicit RecordClassifier(Venue v) noexcept : venue_(v) {}

    Venue venue() const noexcept { return venue_; }

    RecordKind classify(const TraceFrame& f) {
        switch (venue_) {
            case Venue::Anvil:  return anvil_classify(f);
            case Venue::Kraken: return kraken_classify(f, kind_);
        }
        return {};  // unreachable: venue_from_name rejects anything else
    }

private:
    Venue venue_;
    // Both halves are free functions and hold no state; all this object owns is
    // the scratch Kraken's kind name is built in. It deliberately does NOT hold
    // a decoder: classification is the half that costs nothing, and a tool that
    // only counts records should not be constructing an adapter — nor requiring
    // a symbol whose scale this build may not declare.
    std::string kind_;
};

}  // namespace dc::harness
