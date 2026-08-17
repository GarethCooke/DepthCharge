// dc_taxonomy — what is IN a committed trace, and a pin so it cannot move
// quietly.
//
// M4 stage A, deliverable 3. Two jobs:
//
//   * report the record taxonomy of a trace — total records, a count per kind,
//     how many carry no `type`, the two watchdog clocks — for every venue the
//     build knows;
//   * PIN those counts, so that a change to the reader or the decoder that
//     alters what a committed file MEANS fails in the normal build loop.
//
// The second is the point. The counts come out of the C++ reader, so they are
// the reader's own answer to "what is in this file", and a reader change that
// re-partitions 1,599 records is exactly the alarm worth having — that is the
// change that would let a stage-B adapter be covered by a golden that has
// silently stopped describing the trace.
//
// -------------------------------------------------------------------------
// ADD ROWS. NEVER REGENERATE THE TABLE.
// -------------------------------------------------------------------------
// The rule and its reasoning are the owner's, from M4 stage 0, and they are
// copied here rather than cross-referenced because a rule you have to go and
// read is a rule that gets skipped: a new trace's figures are produced by the
// very code these pins exist to guard, so regenerating the table wholesale
// launders a drifted reader into a green suite. The new rows would agree with
// the bug by construction and the old rows — the only thing that could
// contradict it — would be gone. `--pin` therefore REFUSES a trace that already
// has a row, so replacing one means deleting it first, which shows up in a diff
// and wants a sentence in a commit message.
//
// THE PROCEDURE, in order, every time:
//   1. run --selfcheck   — prove every EXISTING row still passes, unmodified
//   2. run --pin <trace> — take the new row from code just shown to be sound
//   3. commit            — the row, the trace and the notes together
//
// An unpinned trace is a FAILURE, not a skip: adding a committed trace without
// recording its figures would leave a green test that measures nothing about
// it. Same rule, same reason, as tools/kraken_frame_economics.py --selfcheck.
//
//   usage: dc_taxonomy [--selfcheck | --pin] <trace.ndjson>...
//
// Exit 0 on success; 1 on a malformed trace, a failed check or a refused pin;
// 2 for a well-formed capture of a venue this build does not know.
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "dc_harness/trace.hpp"
#include "dc_harness/trace_report.hpp"
#include "dc_harness/venue.hpp"

namespace {

using dc::harness::TraceStats;
using dc::harness::Venue;
using dc::harness::venue_traits;

// Everything a row pins. `kinds` is one canonical string rather than a list, so
// a kind appearing, vanishing or being renamed all fail the same comparison —
// there is no way to add a kind the table does not mention.
struct Row {
    const char* file;
    const char* venue;
    std::size_t records;   // every line after the metadata header
    std::size_t tx;        // records this side sent
    std::size_t untyped;   // records whose frame carries no string `type`
    std::size_t book_events;
    std::size_t snapshots;
    std::size_t resyncs;
    // Deliverable 4's measurement, pinned because "legitimate silences no
    // longer read as disconnects" is a claim about numbers.
    // FIXED HISTORICAL REFERENCES, held still on purpose (see trace.hpp). These
    // count gaps against the constants the 2026-08-17 ruling WITHDREW, so the
    // rows keep measuring what they measured when they were pinned and the
    // ruling reads as a delta rather than as a table that changed shape.
    std::size_t wd_declared;       // record-arrival, withdrawn per-venue constant
    std::size_t wd_anvil;          // record-arrival, Anvil's 1000 ms
    std::size_t book_wd_declared;  // book-event, withdrawn per-venue constant
    std::size_t book_wd_anvil;     // book-event, Anvil's 1000 ms
    // ADDED 2026-08-17: the clock that actually decides the grey. Adding a FIELD
    // is not regenerating a ROW — the ten existing figures were re-checked and
    // passed unchanged before this was taken.
    std::size_t liveness_events;
    std::size_t liveness_firings;
    const char* kinds;
};

// ---------------------------------------------------------------------------
// KNOWN_TAXONOMY — measured 2026-08-17 by this program, over the committed
// slices, and reviewed before being pinned.
//
// The four Anvil rows are here as well as the four Kraken ones, and that is
// deliberate: stage A's other claim is that Anvil did not move, and a pin is a
// cheaper way to keep saying so every build than a diff someone has to
// remember to run.
// ---------------------------------------------------------------------------
constexpr Row kKnownTaxonomy[] = {
#include "dc_harness/taxonomy_pins.inc"
};

const Row* find_row(std::string_view file) {
    for (const Row& r : kKnownTaxonomy) {
        if (file == r.file) { return &r; }
    }
    return nullptr;
}

// The basename, so a row is keyed by the trace and not by where it was invoked
// from. Handles both separators: ctest passes an absolute Windows path.
std::string_view basename(std::string_view path) {
    const std::size_t cut = path.find_last_of("/\\");
    return cut == std::string_view::npos ? path : path.substr(cut + 1);
}

// The canonical kinds string. Sorted, because kind_counts is a std::map and a
// pin must not depend on iteration order that could change.
std::string kinds_string(const TraceStats& s) {
    std::string out;
    for (const auto& [kind, n] : s.kind_counts) {
        if (!out.empty()) { out += ' '; }
        out += kind;
        out += '=';
        out += std::to_string(n);
    }
    return out;
}

// What a trace measures as. Deliberately NOT a Row: Row's char pointers are
// string literals in the pin table, and building one out of a trace would mean
// handing back pointers into locals. Compared field by field against a Row
// below.
struct Measured {
    std::string_view venue;
    std::size_t records, tx, untyped, book_events, snapshots, resyncs;
    std::size_t wd_declared, wd_anvil, book_wd_declared, book_wd_anvil;
    std::size_t liveness_events, liveness_firings;
    std::string kinds;
};

Measured measure(const TraceStats& s) {
    Measured m;
    m.venue = venue_traits(s.meta.venue).name;
    m.records = s.frame_count;
    m.tx = s.tx_count;
    m.untyped = s.untyped_records;
    m.book_events = s.book_events;
    m.snapshots = s.snapshot_count;
    m.resyncs = s.mid_stream_snapshots;
    m.wd_declared = s.watchdog_firings_legacy;
    m.wd_anvil = s.watchdog_firings_at_anvil_threshold;
    m.book_wd_declared = s.book_watchdog_firings_legacy;
    m.book_wd_anvil = s.book_watchdog_firings_at_anvil_threshold;
    m.liveness_events = s.liveness_events;
    m.liveness_firings = s.liveness_firings;
    m.kinds = kinds_string(s);
    return m;
}

void print_report(std::string_view file, const TraceStats& s) {
    std::printf("%.*s\n", static_cast<int>(file.size()), file.data());
    print_trace_findings(stdout, s, "  ");
    std::printf("\n");
}

int selfcheck(const std::vector<std::string>& paths) {
    int rc = 0;
    std::size_t checked = 0;
    for (const std::string& path : paths) {
        const std::string_view file = basename(path);
        const TraceStats s = dc::harness::read_trace(path);
        const Measured got = measure(s);
        const Row* want = find_row(file);
        if (want == nullptr) {
            std::printf("  [FAIL] %.*s: no pinned taxonomy on record. Add one with --pin "
                        "rather than leaving this trace unpinned.\n",
                        static_cast<int>(file.size()), file.data());
            rc = 1;
            continue;
        }
        ++checked;

        std::vector<std::string> bad;
        auto cmp = [&bad](const char* what, std::size_t w, std::size_t g) {
            if (w != g) {
                bad.push_back(std::string(what) + ": pinned " + std::to_string(w) +
                              ", measured " + std::to_string(g));
            }
        };
        if (got.venue != want->venue) {
            bad.push_back(std::string("venue: pinned ") + want->venue + ", measured " +
                          std::string(got.venue));
        }
        cmp("records", want->records, got.records);
        cmp("tx", want->tx, got.tx);
        cmp("untyped", want->untyped, got.untyped);
        cmp("book_events", want->book_events, got.book_events);
        cmp("snapshots", want->snapshots, got.snapshots);
        cmp("resyncs", want->resyncs, got.resyncs);
        cmp("wd_declared", want->wd_declared, got.wd_declared);
        cmp("wd_anvil", want->wd_anvil, got.wd_anvil);
        cmp("book_wd_declared", want->book_wd_declared, got.book_wd_declared);
        cmp("book_wd_anvil", want->book_wd_anvil, got.book_wd_anvil);
        cmp("liveness_events", want->liveness_events, got.liveness_events);
        cmp("liveness_firings", want->liveness_firings, got.liveness_firings);
        if (got.kinds != want->kinds) {
            bad.push_back("kinds:\n             pinned   " + std::string(want->kinds) +
                          "\n             measured " + got.kinds);
        }

        if (bad.empty()) {
            std::printf("  [ ok ] %.*s: %zu records, %zu kind(s), taxonomy unchanged\n",
                        static_cast<int>(file.size()), file.data(), got.records,
                        s.kind_counts.size());
        } else {
            rc = 1;
            std::printf("  [FAIL] %.*s\n", static_cast<int>(file.size()), file.data());
            for (const std::string& line : bad) {
                std::printf("           %s\n", line.c_str());
            }
        }
    }
    if (checked == 0) {
        std::fprintf(stderr, "  no traces with a pinned taxonomy were checked\n");
        return 1;
    }
    std::printf("\n  %zu trace(s) checked against KNOWN_TAXONOMY: %s\n", checked,
                rc ? "FAIL" : "all match");
    return rc;
}

int pin(const std::vector<std::string>& paths) {
    // Refuse FIRST, over the whole set, so a mixed invocation prints nothing
    // rather than half a table.
    int rc = 0;
    for (const std::string& path : paths) {
        const std::string_view file = basename(path);
        if (find_row(file) != nullptr) {
            std::fprintf(stderr,
                         "  [REFUSED] %.*s is already pinned. Add rows only. To replace "
                         "this row deliberately, delete it from "
                         "harness/include/dc_harness/taxonomy_pins.inc first.\n",
                         static_cast<int>(file.size()), file.data());
            rc = 1;
        }
    }
    if (rc) { return rc; }

    std::printf("  // paste into taxonomy_pins.inc, having run --selfcheck first\n");
    for (const std::string& path : paths) {
        const std::string_view file = basename(path);
        const TraceStats s = dc::harness::read_trace(path);
        const Measured m = measure(s);
        std::printf("    {\"%.*s\", \"%.*s\",\n"
                    "     /*records=*/%zu, /*tx=*/%zu, /*untyped=*/%zu, "
                    "/*book_events=*/%zu,\n"
                    "     /*snapshots=*/%zu, /*resyncs=*/%zu,\n"
                    "     /*wd_declared=*/%zu, /*wd_anvil=*/%zu, "
                    "/*book_wd_declared=*/%zu, /*book_wd_anvil=*/%zu,\n"
                    "     /*liveness_events=*/%zu, /*liveness_firings=*/%zu,\n"
                    "     \"%s\"},\n",
                    static_cast<int>(file.size()), file.data(),
                    static_cast<int>(m.venue.size()), m.venue.data(), m.records, m.tx,
                    m.untyped, m.book_events, m.snapshots, m.resyncs, m.wd_declared,
                    m.wd_anvil, m.book_wd_declared, m.book_wd_anvil,
                    m.liveness_events, m.liveness_firings, m.kinds.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    enum class Mode { Report, SelfCheck, Pin } mode = Mode::Report;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--selfcheck") {
            mode = Mode::SelfCheck;
        } else if (arg == "--pin") {
            mode = Mode::Pin;
        } else if (!arg.empty() && arg.front() == '-') {
            std::fprintf(stderr, "dc_taxonomy: unknown option %.*s\n",
                         static_cast<int>(arg.size()), arg.data());
            return 1;
        } else {
            paths.emplace_back(arg);
        }
    }
    if (paths.empty()) {
        std::fprintf(stderr,
                     "usage: dc_taxonomy [--selfcheck | --pin] <trace.ndjson>...\n");
        return 1;
    }

    try {
        switch (mode) {
            case Mode::SelfCheck: return selfcheck(paths);
            case Mode::Pin:       return pin(paths);
            case Mode::Report:
                for (const std::string& path : paths) {
                    print_report(basename(path), dc::harness::read_trace(path));
                }
                return 0;
        }
    } catch (const dc::harness::UnknownVenueError& e) {
        std::fprintf(stderr, "dc_taxonomy: %s\n", e.what());
        return 2;
    } catch (const dc::harness::TraceError& e) {
        std::fprintf(stderr, "dc_taxonomy: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "dc_taxonomy: %s\n", e.what());
        return 1;
    }
    return 0;
}
