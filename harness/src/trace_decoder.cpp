// dc_harness/trace_decoder.cpp — Kraken's dialect, the only part that needs a
// parser.
//
// nlohmann/json is included here and in trace.cpp only; both are harness TUs
// and neither is reachable from firmware/ (ARCHITECTURE §7, invariant #7).
//
// THIS PARSES THE VERBATIM FRAME TEXT, not a pre-digested object the reader
// handed down, and that is on purpose even though the reader has already parsed
// the line once. The whole value of `TraceFrame::frame_json` is that a decoder
// sees exactly the bytes the venue sent — key order, number spelling and all —
// because at this venue the CRC32 is computed over the decimal text as sent and
// stage 0 measured 0/2,786 checksums surviving a float round-trip. A classifier
// that read the reader's parse would be a classifier stage B could not grow
// into an adapter without changing where the bytes come from. The second parse
// costs one pass over a 600 KB file on the desk.
#include "dc_harness/trace_decoder.hpp"

#include <nlohmann/json.hpp>

namespace dc::harness {

RecordKind KrakenTraceDecoder::classify(const TraceFrame& f) {
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

}  // namespace dc::harness
