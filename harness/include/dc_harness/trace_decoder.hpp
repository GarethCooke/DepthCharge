// dc_harness/trace_decoder.hpp — the per-venue half of reading a trace.
//
// M4 stage A, deliverable 2. The reader (trace.hpp) owns the ENVELOPE: a line
// is a JSON object with an integer rx_ns, a `frame`, and a monotonic clock. It
// hands on the VERBATIM frame text and knows nothing else. Everything past that
// is dialect, and dialect lives here, one decoder per venue.
//
// M5 STAGE A WIDENED WHAT A DECODER MAY BE ASKED TO CLASSIFY, AND THAT IS THE
// STAGE'S WHOLE MECHANISM (ARCHITECTURE §9, 2026-08-25, the ping row).
//
// `classify` took a `TraceFrame`. It now takes a `TraceRecord`, which may have
// no frame at all: Binance's liveness signal is a WebSocket PING, and a control
// frame is not JSON. The two things that forced it — a ping arrival and a REST
// snapshot body — are one widening rather than two, which is why the rulings put
// them in one stage.
//
// THE ASSERTION MOVED WITH THE CONTRACT. `dc_classifies` below probes
// `classify(const TraceRecord&)`; a decoder still written against `TraceFrame`
// does not merely fail the assertion, it fails to name a type. That is
// deliberate: a widening that left the old assertion passing unchanged would
// have widened nothing — it would have added a second, weaker path beside a
// guard that no longer guarded it.
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
#include <depthcharge/binance/binance_adapter.hpp>
#include <depthcharge/kraken/kraken_adapter.hpp>
#include <depthcharge/symbol.hpp>

#include "dc_harness/trace.hpp"
#include "dc_harness/venue.hpp"

namespace dc::harness {

// What a decoder says about one record. `name` is borrowed from the decoder and
// is valid until its next classify() call — the same lifetime rule
// TraceRecord::frame_json already has, for the same reason (no allocation per
// record on a 2,500-record trace).
//
// There is deliberately no `is_resync` here. Whether a snapshot is a RESYNC is
// a property of the trace, not of the record: it depends on what came before
// it. The reader answers it, venue-independently, in accumulate() — see the
// RESYNC RULE there. Two earlier drafts of this file put it in the decoder and
// got it wrong at Kraken twice, in opposite directions.
struct RecordKind {
    std::string_view name;
    // A FULL REPLACE: after this record the book is fully known and no earlier
    // record is required. `TraceStats` counts these and derives the resync rule
    // from them, so it has to be the re-baselining question and not the looser
    // "replaces something" one.
    //
    // AT BINANCE THAT IS A REST RECORD AND NOTHING ELSE, and it is the one place
    // where this field and `tools/tracefile.py`'s `is_snapshot` deliberately
    // give different answers — the Python twin to mirror is `rebaselines()`,
    // whose own comment predicted this the evening before it happened. A
    // `@depth20` partial payload replaces the top 20 outright and leaves
    // everything below it at whatever it had, so a book maintained from the
    // diff stream is NOT fully known after one; and the venue publishes ~10 of
    // them a second, so counting them here would report a hundred and fifty
    // resyncs on a fifteen-second slice.
    bool is_snapshot = false;
    bool is_book_event = false;  // reaches the book — the AGE clock
    // The venue's declared liveness signal (venue.hpp): Anvil's `summary`,
    // Kraken's `heartbeat`, Binance's `ping`. This is what invariant #5's grey
    // is armed on since the 2026-08-17 ruling — book silence is now age, and age
    // is a number in the header rather than a colour.
    //
    // Binance's is the first that is not a record the venue sent as JSON: it is
    // a WebSocket control frame, reaching this function as a `TraceRecord` with
    // no frame (ARCHITECTURE §9, 2026-08-25).
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
           std::declval<const TraceRecord&>(), std::declval<ContractProbeSink&>()))>>>
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
                                            std::declval<const TraceRecord&>()))>>>
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
                  "a venue decoder must provide RecordKind classify(const TraceRecord&)");
    static_assert(dc_decodes_into_sink<Decoder>::value,
                  "a venue decoder must provide a member template "
                  "`void decode(const TraceRecord&, Sink&)`; a decoder that emits "
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
inline RecordKind anvil_classify(const TraceRecord& f) noexcept {
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

    RecordKind classify(const TraceRecord& f) const noexcept { return anvil_classify(f); }

    template <typename Sink>
    void decode(const TraceRecord& f, Sink&& sink) {
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
// lifetime rule TraceRecord::frame_json already has, for the same reason (no
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
RecordKind kraken_classify(const TraceRecord& f, std::string& scratch);

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

    RecordKind classify(const TraceRecord& f) { return kraken_classify(f, kind_); }

    template <typename Sink>
    void decode(const TraceRecord& f, Sink&& sink) {
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

// ---------------------------------------------------------------------------
// BINANCE — the classifier (M5 stage A). No adapter until B1.
// ---------------------------------------------------------------------------

// Naming a Binance record's kind, and answering the three questions the reader
// asks about it. A FREE FUNCTION for the third time and the same reason:
// `RecordClassifier` at the foot of this file needs it and so does the decoder,
// and a second copy of a rule is how venues drift.
//
// THIS IS THE FIRST CLASSIFIER THAT IS HANDED RECORDS WITH NO FRAME, and the
// three forms are answered in the order that makes the ruling legible:
//
//   control  `is_liveness` is TRUE for a `ping` arrival and false for every
//            other opcode. The kind name is the opcode, so the venue table's
//            `liveness_signal` of "ping" names a kind this function produces —
//            the same relationship Anvil's "summary" and Kraken's "heartbeat"
//            have to theirs.
//   rest     the ONLY thing at this venue that re-baselines. It reaches the
//            book and it is a snapshot.
//   frame    a `depthUpdate` diff or a `@depth20` partial reaches the book;
//            NEITHER is liveness, and that is the ruling as written rather than
//            a simplification of it. Do not soften it into "true for anything
//            unsolicited": the audit stream is unsolicited too, and it is
//            change-driven, which is the entire reason this venue needs a
//            control frame to prove it is alive.
//
// The kind strings are the ones tools/tracefile.py's `record_kind()` produces —
// `depthUpdate`, `partialDepth`, `ack`, plus `rest` and the control opcodes —
// so a histogram printed by a Python tool and one printed by `dc_taxonomy` are
// counting the same buckets.
//
// `scratch` backs the returned `name` until the caller's next call, exactly as
// Kraken's does.
RecordKind binance_classify(const TraceRecord& r, std::string& scratch);

// Binance records -> FeedEvents, through the real adapter (M5 stage B1).
//
// **THE SIGNATURE STAGE A FROZE IS THE SIGNATURE B1 FILLED**, which is the third
// time this seam has been filled without being reshaped: `decode()` kept its
// sink parameter while emitting nothing, precisely so that dropping the adapter
// in would not move it. It did not — the declaration below is unchanged from
// stage A and the body is now `adapter_.on_frame(...)`, the same line Anvil's
// decoder has had since M1 and Kraken's since M4 B1.
//
// THE ONE THING THAT IS GENUINELY NEW HERE, and it is stage A's doing: this
// decoder is handed records that are NOT frames, and it must route them by form
// rather than by content. A REST body goes to `on_rest_body`, a REST record that
// carried none goes to `on_rest_missing` — *the seed has not arrived yet*, which
// is a state and not a failure — and a control frame reaches the adapter at all.
// The other two venues have one entry point because their venues have one kind
// of thing to say.
//
// **THE ADAPTER IS ~96 KiB AND IS HELD BY VALUE.** 32 KiB of ladder, 32 KiB of
// staging frame and 32 KiB of pre-seed buffer, all fixed and never allocated
// (invariant #7 holds). It is fine on the desk and it is a real number for the
// board; where it lives there is D's decision, and binance_adapter.hpp carries
// the measurements behind each of the three.
class BinanceTraceDecoder {
public:
    static constexpr Venue kVenue = Venue::Binance;

    explicit BinanceTraceDecoder(const depthcharge::binance::SymbolConfig& cfg)
        : adapter_(cfg) {}

    depthcharge::binance::BinanceAdapter& adapter() noexcept { return adapter_; }
    const depthcharge::binance::BinanceAdapter& adapter() const noexcept { return adapter_; }

    // See AnvilTraceDecoder::name(). Derived from kVenue, never a second literal.
    static constexpr std::string_view name() noexcept {
        return venue_traits(kVenue).name;
    }

    // Not const only because it owns the scratch `name` borrows from; the
    // classification itself carries no state, which is what lets the same
    // record be classified twice and give the same answer.
    RecordKind classify(const TraceRecord& r) { return binance_classify(r, kind_); }

    template <typename Sink>
    void decode(const TraceRecord& r, Sink&& sink) {
        static_assert(SinkContract<std::remove_reference_t<Sink>>::ok);
        switch (r.form) {
            case RecordForm::Rest:
                // `rest:no-body` is the fetch that produced nothing. It is not a
                // malformed record and it is not a broken feed — the seed has
                // not arrived yet (M5 stage A).
                if (r.has_frame()) { adapter_.on_rest_body(r.frame_json, sink); }
                else { adapter_.on_rest_missing(); }
                return;
            case RecordForm::Control:
                // A ping stamps the liveness clock and reaches no book. The
                // driver reads that from `classify()`; there is nothing here for
                // an adapter to do with it.
                return;
            case RecordForm::Frame:
                break;
        }
        adapter_.on_frame(r.frame_json, sink);
    }

    template <typename Sink>
    void on_transport_gap(depthcharge::GapReason reason, Sink&& sink) {
        static_assert(SinkContract<std::remove_reference_t<Sink>>::ok);
        adapter_.on_transport_gap(reason, sink);
    }

private:
    depthcharge::binance::BinanceAdapter adapter_;
    std::string kind_;  // backs RecordKind::name until the next classify()
};

static_assert(DecoderContract<AnvilTraceDecoder>::ok);
static_assert(DecoderContract<KrakenTraceDecoder>::ok);
static_assert(DecoderContract<BinanceTraceDecoder>::ok);

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

    // NO `default:`, AND NO TRAILING `return {}` (M5 stage A, deliverable 3).
    //
    // This function used to end `return {};  // unreachable`, and the comment
    // was true while the switch was exhaustive — but a `RecordKind{}` is not a
    // refusal, it is "this record reaches nothing, snapshots nothing and proves
    // nothing", which is a confident wrong answer indistinguishable from a real
    // one. A fourth venue added without a branch here would have measured every
    // record in its capture as inert and reported a plausible empty file.
    //
    // Now: the switch lists every enumerator with no default, so a new one is a
    // -Wswitch error under -Werror; and the path past it throws instead of
    // answering. That is the shape fix DESIGN strain 22 asked for — four edits
    // of which only three failed loudly, and this was one of the quiet ones.
    RecordKind classify(const TraceRecord& f) {
        switch (venue_) {
            case Venue::Anvil:   return anvil_classify(f);
            case Venue::Kraken:  return kraken_classify(f, kind_);
            case Venue::Binance: return binance_classify(f, kind_);
        }
        unhandled_venue(venue_, "RecordClassifier::classify");
    }

private:
    Venue venue_;
    // All three halves are free functions and hold no state; all this object
    // owns is the scratch Kraken's and Binance's kind names are built in — one
    // buffer, because exactly one venue is live per classifier and a record is
    // classified before the next one is read. It deliberately does NOT hold
    // a decoder: classification is the half that costs nothing, and a tool that
    // only counts records should not be constructing an adapter — nor requiring
    // a symbol whose scale this build may not declare.
    std::string kind_;
};

}  // namespace dc::harness
