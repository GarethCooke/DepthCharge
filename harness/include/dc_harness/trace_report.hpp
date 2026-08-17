// dc_harness/trace_report.hpp — one rendering of what the reader found.
//
// M4 stage A. Two programs report a trace's findings: `dc_replay` (for a venue
// whose report did not exist before, because the reader could not open the
// file) and `dc_taxonomy`. They were written with a printf block each, and the
// blocks agreed line for line — which is the shape of the bug
// `PINNED_FIELDS` was invented to stop in the Python tool: add a figure to
// `TraceStats`, update one printer, and the other quietly stops reporting
// everything it knows.
//
// So there is one function. Each caller still prints its own surrounding
// context — a filename, or a metadata header — and this prints the findings.
//
// NOT used for the Anvil report in `dc_replay`. That one is deliberately frozen
// byte for byte: stage A's proof that a second venue cost the first one nothing
// is a diff of that program's output over the four committed Anvil traces, and
// a report that grew or moved a line would have made the proof an argument.
#pragma once

#include <cstdio>

#include "dc_harness/trace.hpp"

namespace dc::harness {

// Write the venue, clock, record tally, cadence, book clock, snapshots,
// watchdog comparison and per-kind counts of `s` to `out`, each line prefixed
// with `indent`.
void print_trace_findings(std::FILE* out, const TraceStats& s, const char* indent);

}  // namespace dc::harness
