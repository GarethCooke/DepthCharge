// dc_harness/trace_decoder.cpp — Kraken's dialect, the only part that needs a
// parser.
//
// nlohmann/json is included here and in trace.cpp only; both are harness TUs
// and neither is reachable from firmware/ (ARCHITECTURE §7, invariant #7).
//
// THIS PARSES THE VERBATIM FRAME TEXT, not a pre-digested object the reader
// handed down, and that is on purpose even though the reader has already parsed
// the line once. The whole value of `TraceRecord::frame_json` is that a decoder
// sees exactly the bytes the venue sent — key order, number spelling and all —
// because at this venue the CRC32 is computed over the decimal text as sent and
// stage 0 measured 0/2,786 checksums surviving a float round-trip. A classifier
// that read the reader's parse would be a classifier stage B could not grow
// into an adapter without changing where the bytes come from. The second parse
// costs one pass over a 600 KB file on the desk.
#include "dc_harness/trace_decoder.hpp"

#include <nlohmann/json.hpp>

namespace dc::harness {

RecordKind kraken_classify(const TraceRecord& f, std::string& kind_) {
    using nlohmann::json;

    RecordKind k;
    kind_.clear();

    const json j = json::parse(f.frame_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        // The reader already proved the line is JSON and `frame` is an object,
        // so this is unreachable through TraceReader. Named rather than
        // asserted because classify() is a public entry point and "?" is what
        // the Python tool calls the same thing.
        kind_ = "?";
        k.name = kind_;
        return k;
    }

    const auto channel = j.find("channel");
    if (channel != j.end() && channel->is_string()) {
        const std::string ch = channel->get<std::string>();
        const auto type = j.find("type");
        // `channel/type` where there is a type (`book/update`, `status/update`),
        // the bare channel where there is none (`heartbeat`).
        kind_ = (type != j.end() && type->is_string())
                    ? ch + "/" + type->get<std::string>()
                    : ch;

        k.is_liveness = ch == "heartbeat";
        if (ch == "book") {
            k.is_book_event = true;
            if (type != j.end() && type->is_string() && type->get<std::string>() == "snapshot") {
                k.is_snapshot = true;
            }
        }
        k.name = kind_;
        return k;
    }

    const auto method = j.find("method");
    if (method != j.end() && method->is_string()) {
        const std::string m = method->get<std::string>();
        if (f.is_tx) {
            // A frame this side SENT. The Python reader skips these; here they
            // are named, because a taxonomy that silently drops a record cannot
            // be checked against the file's line count.
            kind_ = "tx:" + m;
        } else {
            const auto success = j.find("success");
            const bool refused = (success != j.end() && success->is_boolean() &&
                                  !success->get<bool>()) ||
                                 j.find("error") != j.end();
            // REFUSED is called out rather than folded into the ack count. A
            // refused subscribe leaves a live socket, a `status` frame and 1 Hz
            // heartbeats over a permanently empty book (NOTES-kraken.md), and an
            // instrument that files it as an ordinary ack cannot find it.
            kind_ = "ack:" + m + (refused ? " REFUSED" : "");
        }
        k.name = kind_;
        return k;
    }

    kind_ = j.find("error") != j.end() ? "error" : "?";
    k.name = kind_;
    return k;
}

// --- Binance (M5 stage A) ----------------------------------------------------
//
// The combined-stream envelope, unwrapped. `/ws/<stream>` delivers the bare
// payload; `/stream?streams=a/b` wraps it as `{"stream": ..., "data": {...}}`
// because one socket carries several streams and the payload alone does not say
// which. Every predicate has to see through it and NONE of them may strip it
// from the record — whether the wrapper is worth its bytes is a measured
// question (M5 stage 0 says yes, 3.2% for a 58.6% saving in confusion) and a
// reader that discarded it would have destroyed the evidence.
//
// The twin is tools/tracefile.py's `binance_payload`.
namespace {

const nlohmann::json& binance_payload(const nlohmann::json& frame) {
    const auto stream = frame.find("stream");
    const auto data = frame.find("data");
    if (stream != frame.end() && data != frame.end() && data->is_object()) { return *data; }
    return frame;
}

}  // namespace

RecordKind binance_classify(const TraceRecord& r, std::string& kind_) {
    using nlohmann::json;

    RecordKind k;
    kind_.clear();

    // A CONTROL RECORD HAS NO FRAME, and this branch is why `classify` had to be
    // widened at all (ARCHITECTURE §9, 2026-08-25). It comes first because it is
    // the only branch that must not touch `frame_json`.
    if (r.form == RecordForm::Control) {
        kind_ = r.ctl.opcode.empty() ? "?" : r.ctl.opcode;
        // THE RULING, VERBATIM: a ping arrival stamps the liveness clock. A pong
        // does not — a pong is this side answering, and a client cannot prove a
        // server alive by talking to it — and neither does a close, which proves
        // the opposite.
        k.is_liveness = r.ctl.opcode == venue_traits(Venue::Binance).liveness_signal;
        k.name = kind_;
        return k;
    }

    // A REST BODY IS THE ONLY THING AT THIS VENUE THAT RE-BASELINES. It is a
    // complete book to its `limit`, stamped with the `lastUpdateId` the diff
    // stream is bracketed against, so a window that begins here is replayable
    // and one that begins anywhere else is not. Named `rest` rather than by the
    // body's shape because a REST body and a `@depth20` payload are the SAME
    // shape — `bids`/`asks`/`lastUpdateId` — and only the record that carries
    // them says which is which.
    if (r.form == RecordForm::Rest) {
        // A FETCH THAT RETURNED NOTHING IS NAMED, NOT FOLDED INTO THE COUNT.
        // Same rule, and the same reason, as Kraken's `ack:subscribe REFUSED`:
        // `capture_binance.py` records a failed fetch because "the snapshot did
        // not arrive" is a fact about the capture window, and filing it as an
        // ordinary fetch would hide the one thing it is evidence of. It
        // re-baselines nothing, because nothing arrived.
        if (!r.has_frame()) {
            kind_ = "rest:no-body";
            k.name = kind_;
            return k;
        }
        kind_ = "rest";
        k.name = kind_;
        k.is_book_event = true;
        k.is_snapshot = true;
        return k;
    }

    const json parsed = json::parse(r.frame_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        // Unreachable through TraceReader, which already proved the line is JSON
        // and `frame` an object. Named rather than asserted because classify()
        // is a public entry point and "?" is what the Python tool calls the same
        // thing.
        kind_ = "?";
        k.name = kind_;
        return k;
    }
    const json& p = binance_payload(parsed);
    if (!p.is_object()) {
        kind_ = "?";
        k.name = kind_;
        return k;
    }

    const auto e = p.find("e");
    if (e != p.end() && e->is_string()) {
        kind_ = e->get<std::string>();  // `depthUpdate`
        // A diff event amends the book; it does not replace it, and it is
        // useless without the REST baseline it is bracketed against.
        k.is_book_event = kind_ == "depthUpdate";
        k.name = kind_;
        return k;
    }
    if (p.find("lastUpdateId") != p.end()) {
        // `@depth20` — no event type on the wire at all; the payload is
        // identified by carrying `lastUpdateId` and no `e`.
        //
        // A BOOK EVENT BUT NOT A SNAPSHOT. It fully determines the top 20 and
        // nothing below it, so the book is not fully known after one. See
        // RecordKind::is_snapshot, where the divergence from the Python
        // predicate of the same name is stated.
        kind_ = "partialDepth";
        k.is_book_event = true;
        k.name = kind_;
        return k;
    }
    if (p.find("result") != p.end() || p.find("id") != p.end()) {
        kind_ = "ack";
        k.name = kind_;
        return k;
    }

    kind_ = "?";
    k.name = kind_;
    return k;
}

}  // namespace dc::harness
