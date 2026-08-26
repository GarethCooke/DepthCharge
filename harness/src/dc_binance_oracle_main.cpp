// dc_binance_oracle — grade the REAL adapter's book against the venue's own.
//
// M5 stage B1, deliverable 7. `tools/binance_oracle.py` grades a PYTHON
// reference implementation and proves the oracle discriminates — it is red
// against three deliberate mutants and green against an honest client. What it
// cannot say anything about is `depthcharge::binance::BinanceAdapter`, which is
// the book that will actually be drawn. This program closes that gap: it drives
// the real adapter over a committed slice and compares its ladder against
// `@depth20`, the venue's own top-20, at every opportunity the wire gives.
//
// WHY THE COMPARISON IS EXACT RATHER THAN APPROXIMATE. `@depth20`'s
// `lastUpdateId` coincided with a diff event's `u` on every payload of every
// complete capture measured at stage 0 — 899/899, 901/901, 90/90, 29/29. The two
// streams are published from the same update boundary, so there is an instant at
// which our book and theirs are statements about the same thing, and the
// comparison needs no tolerance.
//
// THE ACCOUNTING CLOSES, AND THAT IS LOAD-BEARING:
//
//     seen == matched + failed + unverifiable
//
// asserted on every run rather than left as arithmetic in a comment. A fourth
// outcome appearing quietly is exactly the shape of defect that makes a green
// suite say nothing, and `unverifiable` is a LIVE bucket rather than a
// permanently empty one — a window cut on a time boundary can end between the
// two publications of one update, leaving a trailing partial that can never be
// graded. The 15 s `d100ms` slice contains exactly one.
//
// WHAT THIS DOES NOT REACH. `@depth20` validates the top 20 levels a side while
// the panel draws 25. That is the same shape of gap as Kraken's CRC-10-of-25 and
// it is smaller — 20 of 25 against 10 of 25 — but it is not zero, and it is
// printed on every run rather than left in a comment.
//
//   usage: dc_binance_oracle [--depth N] [--verbose] <trace.ndjson>...
//
// Exit 0 if every trace graded GREEN and the accounting closed; 1 otherwise.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <depthcharge/binance/binance_adapter.hpp>
#include <depthcharge/binance/binance_frame.hpp>
#include <depthcharge/feed_event.hpp>

#include "dc_harness/trace.hpp"
#include "dc_harness/venue.hpp"

namespace {

using depthcharge::BookLevel;
using depthcharge::binance::BinanceAdapter;
using depthcharge::binance::BinanceFrame;
using depthcharge::binance::FrameKind;
using depthcharge::binance::FrameSource;
using depthcharge::binance::ParseStatus;
using depthcharge::binance::SymbolConfig;

// One side's top-N, copied out of whichever book it came from.
using Image = std::vector<BookLevel>;
struct BookImage {
    Image bids, asks;
};

bool same(const Image& a, const Image& b) {
    if (a.size() != b.size()) { return false; }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].px != b[i].px || a[i].qty != b[i].qty) { return false; }
    }
    return true;
}

Image top_of(const BookLevel* side, std::uint32_t count, std::uint32_t n) {
    Image out;
    const std::uint32_t take = count < n ? count : n;
    out.reserve(take);
    for (std::uint32_t i = 0; i < take; ++i) { out.push_back(side[i]); }
    return out;
}

// The three disjoint outcomes, and nothing else may exist.
struct Outcome {
    std::size_t seen = 0, matched = 0, failed = 0, unverifiable = 0;
    std::map<std::string, std::size_t> reasons;
    std::string first_failure;

    void note(bool ok, const std::string& detail) {
        ++seen;
        if (ok) { ++matched; return; }
        ++failed;
        if (first_failure.empty()) { first_failure = detail; }
    }
    void note_unverifiable(const char* why) {
        ++seen;
        ++unverifiable;
        ++reasons[why];
    }
    bool closes() const { return seen == matched + failed + unverifiable; }
    bool green() const { return failed == 0 && matched > 0; }
};

// ---------------------------------------------------------------------------
// THE PIN. Measured 2026-08-25 by this program, against the committed slices.
// ---------------------------------------------------------------------------
//
// ADD ROWS, NEVER REGENERATE THE TABLE — the rule dc_taxonomy_main.cpp states at
// length and for the same reason: these figures come out of the very code they
// exist to guard, so regenerating them wholesale launders a drifted adapter into
// a green suite.
//
// **EVERY MATCHED COUNT HERE EQUALS `tools/binance_oracle.py`'s FOR THE SAME
// SLICE**, and that agreement is the strongest single statement this stage
// makes: two implementations, in two languages, written against the same wire by
// different means, grade identically at every opportunity the venue gives. The
// Python one is the reference the mutants are run against; this one is the book
// that will be drawn.
//
// The two tools' `seen` deliberately differ. A partial arriving BEFORE the seed
// is skipped entirely by the Python tool and counted `unverifiable` here — it is
// a thing that happened and could not be graded, which is what that bucket is
// for. The matched and failed counts, which are the ones that mean anything, are
// identical.
struct Expect {
    const char* file;
    std::size_t matched;
    std::size_t failed;
    std::size_t unverifiable;
    const char* note;
};

constexpr Expect kExpect[] = {
    // The two deep-seed witnesses: `limit=1000`, and the oracle's headline.
    {"binance_btcusdt_deepseed_20260824.ndjson", 235, 0, 15,
     "the oracle golden -- 235/235, identical to binance_oracle.py's honest control"},
    {"binance_btcusdt_deepseed2_20260824.ndjson", 235, 0, 15,
     "the second, independent witness -- a later window the same evening"},
    {"binance_atomeur_deepseed_20260824.ndjson", 8, 0, 0,
     "the quiet pair: only 8 graded ticks in 47 s, and all 8 match"},
    // `limit=100` seeds. The 15 s and quiet-pair windows are short enough that
    // 100 levels still hold; the 60 s one is not, and that is the finding.
    {"binance_btcusdt_d100ms_20260824.ndjson", 140, 0, 11,
     "15 s at limit=100 -- short enough that the seed still covers the walk"},
    {"binance_atomeur_d100ms_20260824.ndjson", 29, 0, 0,
     "the quiet pair barely moves, so limit=100 is ample here"},
    // **RED ON PURPOSE, AND NOT THIS ADAPTER'S DEFECT.** Captured at
    // `rest_limit: 100`, which stage 0 measured as wrong within 90 seconds on
    // BTCUSDT: 100 levels is a $16 price window and the pair walked $29.85 in
    // that time, so the book cannot refill from a seed that never contained the
    // levels the market walked onto. `binance_oracle.py` grades this slice
    // 28 matched / 32 failed and so does this program -- to the exact count,
    // which is what makes it a reproduction rather than a coincidence. It is the
    // reason the brief mandates limit=1000 and it is kept as the witness for it.
    {"binance_btcusdt_d1000ms_20260824.ndjson", 28, 32, 1,
     "RED BY CONSTRUCTION: seeded at limit=100 over a 60 s window; the reference "
     "implementation fails the identical 32"},
    // No audit stream in the capture at all: `/ws/btcusdt@depth@100ms` is a
    // single-stream URL, so there is no `@depth20` to grade against. VACUOUS is
    // the honest verdict and is distinct from GREEN -- a capture with nothing to
    // disagree about must not read as a passing one.
    {"binance_btcusdt_reconnect_20260824.ndjson", 0, 0, 0,
     "VACUOUS: single-stream capture, no @depth20, nothing to grade"},
    // --- THE RULING'S EVIDENCE, AND THE FIRST GRADING AT THE BOARD'S CADENCE --
    // (M5 stage B2.) The two complete 90 s captures the 2026-08-25 audit-stream
    // ruling was signed on, committed whole rather than sliced -- see the
    // taxonomy rows for why a window cut out of one would evidence a different
    // claim. Both are `limit=1000` on the liquid pair with `@depth20` at the
    // **1000 ms tick**, which is the configuration decision 2 puts on the board
    // and which no row above tests: every other witness runs the audit stream at
    // 100 ms.
    //
    // 88/88 both times, and it is the SAME 88 the ruling quotes and the same 88
    // `tools/binance_oracle.py` reports -- so the figure is now reproduced by two
    // implementations out of a committed file, which is what the clause asked
    // for. The 2 unverifiable are the two partial payloads that arrive before the
    // seed lands; the Python tool skips those rather than counting them, which is
    // the documented `seen` difference above and not a disagreement.
    {"binance_btcusdt_mixed1_20260825.ndjson", 88, 0, 2,
     "the ruling's first witness, whole: 90/90 coincidence, 88/88 GREEN at the "
     "1000 ms audit tick"},
    {"binance_btcusdt_mixed2_20260825.ndjson", 88, 0, 2,
     "the ruling's second, independent witness, whole -- identical figures an "
     "hour apart"},
};

const Expect* expected_for(const std::string& file) {
    for (const Expect& e : kExpect) {
        if (file == e.file) { return &e; }
    }
    return nullptr;
}

class Grader {
public:
    Grader(const SymbolConfig& cfg, std::uint32_t depth)
        : depth_(depth), adapter_(std::make_unique<BinanceAdapter>(cfg)),
          frame_(std::make_unique<BinanceFrame>()), cfg_(cfg) {}

    Outcome& outcome() noexcept { return out_; }
    const BinanceAdapter& adapter() const noexcept { return *adapter_; }

    void run(dc::harness::TraceReader& reader) {
        // The adapter's events are consumed and counted but not re-books here:
        // the thing under test is the ADAPTER'S ladder, and rebuilding a second
        // book from its FeedEvents would grade that reconstruction instead. The
        // engine's mirror is `test_replay_goldens`'s business.
        auto sink = [this](const depthcharge::FeedEvent&) { ++events_; };

        dc::harness::TraceRecord rec;
        while (reader.next(rec)) {
            switch (rec.form) {
                case dc::harness::RecordForm::Control:
                    continue;
                case dc::harness::RecordForm::Rest:
                    if (rec.has_frame()) { adapter_->on_rest_body(rec.frame_json, sink); }
                    else { adapter_->on_rest_missing(); }
                    continue;
                case dc::harness::RecordForm::Frame:
                    break;
            }

            // Parsed a second time, here, on purpose: the adapter does not
            // expose its frame and must not — a grader that read the adapter's
            // private parse would be comparing the adapter against itself.
            const ParseStatus st = parse_binance_frame(rec.frame_json, FrameSource::WsFrame,
                                                       cfg_, *frame_);
            adapter_->on_frame(rec.frame_json, sink);
            if (st != ParseStatus::Ok) { continue; }

            if (frame_->kind == FrameKind::DepthUpdate) {
                if (adapter_->has_baseline()) { on_applied(frame_->final_update_id); }
            } else if (frame_->kind == FrameKind::PartialDepth &&
                       frame_->has_last_update_id) {
                on_partial();
            }
        }

        // Partials still held when the trace ended never got their diff.
        for (const auto& [id, img] : pending_) {
            (void)id;
            (void)img;
            out_.note_unverifiable("never-reached-before-capture-ended");
        }
        pending_.clear();
    }

    std::size_t events() const noexcept { return events_; }

private:
    BookImage live_image() const {
        return BookImage{top_of(adapter_->bids(), adapter_->bid_count(), depth_),
                         top_of(adapter_->asks(), adapter_->ask_count(), depth_)};
    }

    // A diff has just been applied and the book now stands at `u`. Bank the
    // image so a REST body or a late partial naming this instant can still be
    // graded against it, and release anything that was waiting for it.
    void on_applied(std::int64_t u) {
        history_[u] = live_image();
        if (history_.size() > 8000) { history_.erase(history_.begin()); }
        const auto held = pending_.find(u);
        if (held != pending_.end()) {
            grade(held->second, live_image(), "partial(deferred)");
            pending_.erase(held);
        }
        // Anything still pending that this event has overshot can never be
        // graded: a coalesced event stepped straight over the id it names.
        while (!pending_.empty() && pending_.begin()->first < u) {
            out_.note_unverifiable("overshot-by-a-coalesced-event");
            pending_.erase(pending_.begin());
        }
    }

    void on_partial() {
        if (!adapter_->has_baseline()) {
            // Before the seed there is no book to compare against, and saying so
            // is different from saying the comparison failed.
            out_.note_unverifiable("before-the-baseline");
            return;
        }
        BookImage theirs{top_of(frame_->bids, frame_->bid_count, depth_),
                         top_of(frame_->asks, frame_->ask_count, depth_)};
        const std::int64_t L = frame_->last_update_id;
        const std::int64_t at = adapter_->last_update_id();
        if (L == at) {
            grade(theirs, live_image(), "partial");
            return;
        }
        if (L > at) {
            // Arrived before the diff that ends on it. The two streams are
            // interleaved on one socket and neither is owed the other's
            // ordering, so hold it and grade when that diff lands.
            pending_[L] = std::move(theirs);
            return;
        }
        const auto seen = history_.find(L);
        if (seen != history_.end()) {
            grade(theirs, seen->second, "partial(historic)");
            return;
        }
        out_.note_unverifiable("no-event-ends-on-this-id");
    }

    void grade(const BookImage& theirs, const BookImage& ours, const char* tag) {
        const bool ok = same(theirs.bids, ours.bids) && same(theirs.asks, ours.asks);
        std::string detail;
        if (!ok) {
            char buf[256];
            std::snprintf(buf, sizeof buf,
                          "%s: theirs %zu/%zu levels, ours %zu/%zu", tag, theirs.bids.size(),
                          theirs.asks.size(), ours.bids.size(), ours.asks.size());
            detail = buf;
            for (std::size_t i = 0; i < theirs.bids.size() && i < ours.bids.size(); ++i) {
                if (theirs.bids[i].px != ours.bids[i].px ||
                    theirs.bids[i].qty != ours.bids[i].qty) {
                    std::snprintf(buf, sizeof buf, "  first bid divergence at rank %zu: "
                                  "theirs %lld@%lld ours %lld@%lld",
                                  i, static_cast<long long>(theirs.bids[i].px),
                                  static_cast<long long>(theirs.bids[i].qty),
                                  static_cast<long long>(ours.bids[i].px),
                                  static_cast<long long>(ours.bids[i].qty));
                    detail += buf;
                    break;
                }
            }
        }
        out_.note(ok, detail);
    }

    std::uint32_t depth_;
    std::unique_ptr<BinanceAdapter> adapter_;   // ~96 KiB; never on the stack
    std::unique_ptr<BinanceFrame> frame_;       // 32 KiB, the grader's own parse
    SymbolConfig cfg_;
    Outcome out_;
    std::map<std::int64_t, BookImage> history_;
    std::map<std::int64_t, BookImage> pending_;
    std::size_t events_ = 0;
};

int grade_trace(const std::string& path, std::uint32_t depth, bool verbose, bool& green) {
    dc::harness::TraceReader reader(path);
    if (reader.venue() != dc::harness::Venue::Binance) {
        std::fprintf(stderr, "  %s: not a Binance trace\n", path.c_str());
        return 1;
    }
    SymbolConfig cfg{};
    if (!depthcharge::binance::symbol_config_for(reader.meta().symbol, cfg)) {
        std::fprintf(stderr, "  %s: no declared scale for symbol \"%s\"\n", path.c_str(),
                     reader.meta().symbol.c_str());
        return 1;
    }

    Grader g(cfg, depth);
    g.run(reader);
    const Outcome& o = g.outcome();
    const auto& st = g.adapter().stats();

    const char* verdict = o.failed > 0 ? "RED" : (o.matched > 0 ? "GREEN" : "VACUOUS");
    std::printf("  %-44s seen %4zu  matched %4zu (%5.1f%%)  failed %3zu  "
                "unverifiable %3zu  => %s\n",
                path.substr(path.find_last_of("/\\") + 1).c_str(), o.seen, o.matched,
                o.seen ? 100.0 * static_cast<double>(o.matched) / static_cast<double>(o.seen)
                       : 0.0,
                o.failed, o.unverifiable, verdict);
    if (!o.closes()) {
        std::printf("    [FAIL] accounting does not close: %zu != %zu + %zu + %zu\n", o.seen,
                    o.matched, o.failed, o.unverifiable);
        green = false;
        return 1;
    }
    if (!o.first_failure.empty()) { std::printf("    first failure: %s\n", o.first_failure.c_str()); }
    for (const auto& [why, n] : o.reasons) {
        std::printf("    unverifiable: %-36s %zu\n", why.c_str(), n);
    }
    if (verbose) {
        std::printf("    seed: bracket ok=%llu failed=%llu   buffered=%llu dropped=%llu "
                    "overflow=%llu\n",
                    static_cast<unsigned long long>(st.seed_bracket_ok),
                    static_cast<unsigned long long>(st.seed_bracket_failed),
                    static_cast<unsigned long long>(st.buffered_events),
                    static_cast<unsigned long long>(st.buffered_dropped_by_seed),
                    static_cast<unsigned long long>(st.buffer_overflows));
        std::printf("    U/u: seq_breaks=%llu   levels: applied=%llu removed=%llu "
                    "absent=%llu unchanged=%llu evicted=%llu\n",
                    static_cast<unsigned long long>(st.seq_breaks),
                    static_cast<unsigned long long>(st.levels_applied),
                    static_cast<unsigned long long>(st.levels_removed),
                    static_cast<unsigned long long>(st.levels_absent_removals),
                    static_cast<unsigned long long>(st.levels_unchanged),
                    static_cast<unsigned long long>(st.levels_evicted));
        std::printf("    window: exits=%llu entries=%llu outside_emit=%llu   "
                    "book low-water bid=%u ask=%u\n",
                    static_cast<unsigned long long>(st.window_exits),
                    static_cast<unsigned long long>(st.window_entries),
                    static_cast<unsigned long long>(st.levels_outside_emit),
                    st.min_bid_levels, st.min_ask_levels);
    }
    if (o.failed > 0 || o.matched == 0) { green = false; }
    return 0;
}

// --check: every slice must grade EXACTLY as pinned. Not "green" — exactly as
// pinned, because one of the seven is red for a documented reason and a mode
// that demanded green everywhere would have to either drop that slice or lie
// about it. An unpinned trace is a FAILURE and not a skip, the same rule
// dc_taxonomy holds for the same reason.
int check_trace(const std::string& path, std::uint32_t depth) {
    const std::string file = path.substr(path.find_last_of("/\\") + 1);
    dc::harness::TraceReader reader(path);
    SymbolConfig cfg{};
    if (!depthcharge::binance::symbol_config_for(reader.meta().symbol, cfg)) {
        std::printf("  [FAIL] %s: no declared scale for \"%s\"\n", file.c_str(),
                    reader.meta().symbol.c_str());
        return 1;
    }
    Grader g(cfg, depth);
    g.run(reader);
    const Outcome& o = g.outcome();
    const Expect* want = expected_for(file);
    if (want == nullptr) {
        std::printf("  [FAIL] %s: no pinned oracle result on record. Add a row rather "
                    "than leaving this trace ungraded.\n", file.c_str());
        return 1;
    }
    if (!o.closes()) {
        std::printf("  [FAIL] %s: accounting does not close: %zu != %zu + %zu + %zu\n",
                    file.c_str(), o.seen, o.matched, o.failed, o.unverifiable);
        return 1;
    }
    if (o.matched != want->matched || o.failed != want->failed ||
        o.unverifiable != want->unverifiable) {
        std::printf("  [FAIL] %s\n"
                    "           pinned   matched %zu failed %zu unverifiable %zu\n"
                    "           measured matched %zu failed %zu unverifiable %zu\n"
                    "           %s\n",
                    file.c_str(), want->matched, want->failed, want->unverifiable, o.matched,
                    o.failed, o.unverifiable, want->note);
        if (!o.first_failure.empty()) {
            std::printf("           first failure: %s\n", o.first_failure.c_str());
        }
        return 1;
    }
    std::printf("  [ ok ] %-44s matched %4zu  failed %3zu  unverifiable %3zu\n", file.c_str(),
                o.matched, o.failed, o.unverifiable);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint32_t depth = 20;
    bool verbose = false;
    bool check = false;
    std::vector<std::string> traces;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--verbose") { verbose = true; }
        else if (a == "--check") { check = true; }
        else if (a == "--depth" && i + 1 < argc) { depth = static_cast<std::uint32_t>(std::atoi(argv[++i])); }
        else { traces.push_back(a); }
    }
    if (traces.empty()) {
        std::fprintf(stderr, "usage: dc_binance_oracle [--depth N] [--verbose] <trace>...\n");
        return 1;
    }

    std::printf("dc_binance_oracle — the REAL adapter, graded against @depth%u\n\n", depth);
    bool green = true;
    int rc = 0;
    for (const std::string& t : traces) {
        try {
            rc |= check ? check_trace(t, depth) : grade_trace(t, depth, verbose, green);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  %s: %s\n", t.c_str(), e.what());
            rc = 1;
        }
    }
    if (check) {
        std::printf("\n  %zu slice(s) checked against the pinned oracle results: %s\n",
                    traces.size(), rc ? "FAIL" : "all match");
        return rc;
    }
    std::printf("\n  the oracle validates the top %u levels a side; the panel draws 25.\n",
                depth);
    if (!green) { std::printf("  NOT GREEN\n"); }
    return (rc != 0 || !green) ? 1 : 0;
}
