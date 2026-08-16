// firmware/src/ws_ping.hpp — how deep is OUR queue on the server?
//
// THE ONE QUESTION THE OTHER INSTRUMENTS CANNOT SEPARATE.
//
// `staleness.hpp` answers "how old is the book" and answers it well. What it
// cannot say is WHOSE fault the age is, and there are two very different
// answers with the same symptom:
//
//   * this socket's server-side write queue is deep — Anvil has serialised the
//     frames and they are sitting in Crow's `write_buffers_` waiting for our
//     transatlantic drain to take them. The fix is fewer bytes (ROADMAP A7).
//   * the queue is shallow and the lag is UPSTREAM — the broadcaster itself is
//     behind, or our own pipeline is. Fewer bytes would not help at all.
//
// A round-trip separates them, and it costs six bytes.
//
// WHY A PONG PRICES THE QUEUE, STATED PRECISELY BECAUSE THE LOOSE VERSION IS
// WRONG IN THE DIRECTION THAT WOULD MISLEAD US.
//
// Read out of Anvil's vendored Crow (`server/third_party/crow_all.h`, confirmed
// 2026-08-16): a client PING is answered by the library with no application
// code — control frames never reach `onmessage` — and the pong is appended to
// the *same* per-connection `write_buffers_` as data. So the pong **cannot
// overtake any frame already queued on this connection**, and the round-trip is
//
//     network RTT  +  the time to drain everything queued ahead of the pong
//
// which is strictly stronger than TCP liveness. What it is NOT:
//
//   * It is NOT a freshness measurement. Frames still upstream in the
//     broadcaster have not been posted to this connection yet, and a pong WILL
//     be delivered ahead of them. If the fan-out is what lags, this reads fast
//     while the book is stale. **A fast pong never means the book is fresh.**
//   * The ordering is one-sided: a data frame posted concurrently with the
//     ping's arrival may land either side of the pong. That is noise of one
//     frame, not of one queue.
//   * It is read from Anvil's source and has never been captured under induced
//     backpressure. ROADMAP A2 asks Anvil to treat the ordering as a contract —
//     they never claimed it, it is stock vendored Crow, and two things would
//     silently break it (a ping after a two-way close handshake is never
//     ponged; the behaviour depends on `max_payload` staying default with
//     `CROW_ENFORCE_WS_SPEC` undefined). Until that sentence exists, read this
//     number as evidence and not as a guarantee.
//
// SO THE INSTRUMENT IS A PAIR, AND THE PAIR IS THE DIAGNOSIS. Read the `-- ping`
// line beside `-- age`:
//
//     age high, rtt high   this socket's queue is the cause — we cannot drain
//                          what Anvil is sending. A7 is the fix.
//     age high, rtt low    the queue is shallow; the lag is upstream of it.
//                          A7 would not help and something else is wrong.
//     age low,  rtt high   a burst is draining. Transient by construction.
//     age low,  rtt low    healthy.
//
// Neither number alone supports either conclusion, which is the whole reason
// this file exists rather than a second staleness field.
//
// ============================================================================
// NOTHING BRANCHES ON THIS, AND THAT IS INVARIANT #5, NOT TIDINESS.
// ============================================================================
//
// A healthy round-trip must never un-grey a panel. §6 #5 says liveness is
// defined by events reaching the BOOK, and a pong is not a book event — a
// server can answer pongs perfectly while publishing nothing, which is exactly
// the 2 min 56 s stall of 2026-08-16 00:12. An instrument that could turn the
// panel green on control traffic would be a frozen ladder reading LIVE, the one
// output the constitution forbids. So this class exposes counts and durations
// and NO predicate: there is deliberately no `healthy()`, no `live()` and no
// bool for a caller to mistake for one.
//
// It does not drop sockets either. A ping that is never answered is recorded
// and nothing else happens; `WsSupervisor::kSilenceRecycleUs` already owns
// recovery, on a rule that needs no server cooperation. Killing a socket on a
// missing pong would add a novel way for this firmware to drop a healthy
// connection in exchange for catching nothing the recycle does not already
// catch — which is the deleted refused-retry fast path's mistake, in a new file.
//
// NO ESP-IDF, DELIBERATELY — the sixth instrument written to that rule, after
// frame_reassembler.hpp, gap_histogram.hpp, stall_probe.hpp, ws_supervisor.hpp
// and staleness.hpp. Every clock value is a parameter, so
// `harness/tests/test_ws_ping.cpp` drives the identical code the board runs.
//
// SINGLE WRITER: the RX task sends, the RX task parses the pong, so both halves
// of every round-trip are stamped by one task and no field here is written from
// two places. The render task on Core 1 only reads, through `render()`.
//
// **AND THE TORN-READ CLAIM IS THE NARROW ONE, NOT THE BROAD ONE. An earlier
// draft of this comment said the single writer put this file "in a stronger
// position than any other counter in this firmware", and that was wrong — it is
// the same position `staleness.hpp` is in, and that file says so at length and
// tells the next one not to copy the other instruments' sentence. This is the
// next one.** `gap_histogram.hpp` and `stall_probe.hpp` can say "stale, never
// torn" because every field they expose across cores is 32-bit and naturally
// aligned. Split this object's fields the same way and the answer differs by
// kind:
//
//   * The DURATIONS — `last_rtt_us_`, `worst_rtt_us_`, `worst_rtt_ever_us_` —
//     are 64-bit but are bounded small, because a round-trip cannot outlive its
//     socket and `kSilenceRecycleUs` recycles that socket at five minutes. Three
//     hundred million microseconds needs 29 bits, so the high word of every one
//     of them is zero on both sides of any store and a tear is unobservable.
//     That is a property of the recycle, so it is stated here rather than
//     assumed: if the recycle ever grows past ~71.6 minutes, this paragraph
//     stops being true and the one below starts applying to these fields too.
//   * The TIMESTAMPS — `ping_sent_us_`, read across cores by `outstanding_us()`
//     — carry no such bound. `esp_timer_get_time()` stays under 2^32 µs only for
//     the first 71.6 minutes of uptime, and past that a `note_ping_sent` racing
//     a `render()` can put one absurd `waiting` figure on one log line. That is
//     the same cost every counter in this firmware already accepts, so it is a
//     bound rather than a defect — but it is a bound, and an instrument whose
//     failure mode is a confident wrong number does not get to leave it unsaid.
//
// The counters (`pings_`, `pongs_`, `unsolicited_`, `unanswered_`) are 32-bit
// and are genuinely stale-never-torn.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "gap_histogram.hpp"   // append_truncating

namespace depthcharge::fw {

// The build switch, and it lives HERE rather than in ws_transport.hpp because
// the thing it switches lives here — and because `render()` below has to know.
//
// Without it the pingless arm prints "no round-trip yet" on every statistics
// block forever, which is indistinguishable on the line from a venue that is
// being asked and is not answering. Those are opposite findings. A build flag
// that makes a diagnostic line lie about which of the two you are looking at is
// worse than no flag, so the arm says what it is.
//
// `depthcharge-noping` sets it to 0; the default build pings. See
// `firmware/platformio.ini` for why the arm inverted rather than being deleted.
#ifndef DC_WS_PING
#define DC_WS_PING 1
#endif
inline constexpr bool kClientPingEnabled = (DC_WS_PING != 0);

// How long after a completed round-trip the next ping may go out.
//
// TEN SECONDS, AND IT IS A FLOOR RATHER THAN A RATE. Only one ping is ever in
// flight (see `ping_due`), so a socket whose queue is 90 s deep produces one
// reading every 90 s and this constant never binds. What it bounds is the
// HEALTHY case, and three things size it:
//
//   * It matches the statistics block's own 10 s window, so a healthy socket
//     contributes about one completed reading per printed line. A shorter
//     period would put several readings behind one printed number with no way
//     to tell them apart; a much longer one leaves windows with no reading at
//     all and a `-- ping` line that is mostly stale.
//   * The wire cost is nil — six bytes out, two back — against a stream
//     measured at 56-87 KiB/s.
//   * The real lower bound is not latency, it is WRITE PRESSURE ON THE RX TASK.
//     `write_all` can block up to `kWriteBudgetUs`, and that time is taken from
//     a loop the 23.6 h soak measured as `read 98-99%` — i.e. one that is bound
//     by how fast bytes arrive. Ten seconds makes the write a rounding error;
//     one second would not, and would buy nothing, because nothing branches on
//     the result and no decision is waiting for it.
inline constexpr std::int64_t kPingPeriodUs = 10 * 1000 * 1000;

// Round-trips are reported in MILLISECONDS, not in tenths of a second like
// every other duration this firmware prints, and the reason is the range.
//
// The healthy reading is the transatlantic RTT — **87 ms**, median of eight TCP
// connects to 52.204.246.224 on 2026-08-11 — which `staleness.hpp`'s SecondsText
// renders as "0.0 s", i.e. as nothing at all. The interesting readings run from
// there to the 111 s of measured backlog, five orders of magnitude up. One
// integer unit across the whole span beats a conditional format that is right
// twice and confusing once, and 111 s prints as 111000 ms, which is ugly and
// unambiguous — the correct trade for a line read for a verdict.
//
// `kUsPerMs` itself is in gap_histogram.hpp beside the other shared helpers,
// because this file was the fifth place to need it and the first to name it.

class PingProbe {
public:
    // A socket came up. Everything about the previous connection's queue died
    // with it, so every reading does too — the same rule, and the same reason,
    // as StalenessEstimator::note_connect.
    //
    // The cadence clock starts here, so the first ping of a connection goes out
    // one period in rather than immediately. That is deliberate: a connect is
    // followed by Anvil's snapshot and the first book frames, and a ping racing
    // them would measure the snapshot's own drain and report it as backlog.
    void note_connect(std::int64_t at_us) noexcept {
        idle_since_us_ = at_us;
        in_flight_ = false;
        ping_sent_us_ = 0;
        last_rtt_us_ = 0;
        worst_rtt_us_ = 0;
        // `worst_rtt_ever_us_` is deliberately NOT reset — see its accessor. It
        // is the one field that must outlive the socket, because the reading it
        // most often holds was taken at the death of the previous one.
        pings_ = 0;
        pongs_ = 0;
        unsolicited_ = 0;
        unanswered_ = 0;
        ++connections_;
    }

    // ONE PING IN FLIGHT AT A TIME, AND THIS IS THE CORRECTNESS ARGUMENT FOR THE
    // WHOLE FILE.
    //
    // RFC 6455 §5.5.2: "If an endpoint receives a Ping frame and has not yet
    // sent Pong frame(s) in response to previous Ping frame(s), the endpoint MAY
    // elect to send a Pong frame for only the most recently processed Ping
    // frame." So a peer is entitled to answer two outstanding pings with ONE
    // pong, and there is then no way to know which one it answered. Pairing a
    // pong with the oldest outstanding ping would over-report the round-trip;
    // pairing it with the newest would under-report it. Both are fabrications.
    //
    // Sending one at a time removes the ambiguity instead of guessing at it: an
    // arriving pong can only be the answer to the one ping outstanding, and the
    // subtraction is exact. It also self-paces the instrument onto the very
    // quantity it is measuring — a deep queue slows the readings down, which is
    // the honest behaviour and needs no timeout to arrange.
    //
    // The alternative was a sequence number in the ping payload (§5.5.2 obliges
    // the pong to echo the application data byte for byte, so correlation would
    // work). It is rejected because the coalescing rule above means the older
    // pings would still never be answered — a correlation key does not create a
    // response — so it would buy nothing and cost a payload path in the one
    // place this transport writes to the socket.
    bool ping_due(std::int64_t now_us) const noexcept {
        if (in_flight_) { return false; }
        return (now_us - idle_since_us_) >= kPingPeriodUs;
    }

    void note_ping_sent(std::int64_t at_us) noexcept {
        in_flight_ = true;
        ping_sent_us_ = at_us;
        ++pings_;
    }

    // A pong arrived.
    //
    // A PONG WITH NOTHING OUTSTANDING IS NOT A READING. RFC 6455 §5.5.3: "A Pong
    // frame MAY be sent unsolicited. This serves as a unidirectional heartbeat."
    // Treating one as the answer to a ping we never sent would compute a
    // round-trip against a stale or zero timestamp and print a number that is
    // pure invention — small and plausible, which is the worst kind. It is
    // counted instead, because an unsolicited pong from Anvil would itself be
    // news: nothing in the vendored Crow sends one, so a non-zero count here
    // means the server is not the server this file was written against.
    void note_pong(std::int64_t at_us) noexcept {
        if (!in_flight_) {
            ++unsolicited_;
            return;
        }
        in_flight_ = false;
        idle_since_us_ = at_us;
        // The clock cannot run backwards on esp_timer_get_time(), so this is
        // defence against a caller mistake — but an unsigned subtraction in the
        // wrong order would print an 18-quintillion-millisecond round-trip and
        // read as a catastrophic finding. The same guard, for the same reason,
        // as StalenessEstimator::note_summary.
        if (at_us < ping_sent_us_) { return; }
        ++pongs_;
        last_rtt_us_ = static_cast<std::uint64_t>(at_us - ping_sent_us_);
        bank_peak(last_rtt_us_);
    }

    // The socket went down with a ping still out.
    //
    // Called from the death path so the episode survives it — the same defect
    // StalenessEstimator::note_disconnect exists to fix, and it would have
    // arrived here identically: a queue deep enough to outlive the socket is the
    // most interesting reading this instrument can take, and it is exactly the
    // one that never completes.
    //
    // The outstanding time is banked into `worst`, because a round-trip that had
    // already run N seconds when the socket died is a lower bound on a real
    // round-trip of at least N. Counting it as unanswered as well is what stops
    // that lower bound being read as a completed measurement.
    void note_disconnect(std::int64_t at_us) noexcept {
        if (!in_flight_) { return; }
        bank_peak(outstanding_us(at_us));
        ++unanswered_;
        in_flight_ = false;
    }

    // How long the ping currently in flight has been waiting. Zero when none is.
    //
    // This is the LIVE half of the instrument and the more useful one during an
    // incident: `last_rtt` is the last completed round-trip and can be minutes
    // old, whereas this says what the queue is doing right now. It is a lower
    // bound on the round-trip in progress, and it only grows.
    std::uint64_t outstanding_us(std::int64_t now_us) const noexcept {
        if (!in_flight_ || now_us <= ping_sent_us_) { return 0; }
        return static_cast<std::uint64_t>(now_us - ping_sent_us_);
    }

    bool in_flight() const noexcept { return in_flight_; }
    // True once a round-trip has completed on this connection. Until then there
    // is no reading at all, which the console must say rather than print 0 ms —
    // "no reading yet" and "the queue is empty" are different statements and
    // exactly one of them is reassuring. Same rule as StalenessEstimator.
    bool measured() const noexcept { return pongs_ != 0; }

    std::uint64_t last_rtt_us() const noexcept { return last_rtt_us_; }
    std::uint64_t worst_rtt_us() const noexcept { return worst_rtt_us_; }
    // Retained across connects: the headline number for a whole run, which the
    // per-connection figure destroys every time the socket blinks.
    //
    // THIS IS THE SIBLING INSTRUMENT'S SCAR, NOT A NEW IDEA.
    // StalenessEstimator shipped without it and the 86-minute run of 2026-08-09
    // looked healthy because 21 reconnects erased the finding every four
    // minutes. The same hole was open here and is worse in one respect: the
    // deepest reading this probe can take is a round-trip that OUTLIVES its
    // socket, so the single most interesting datum arrives at `note_disconnect`
    // and was then zeroed by the `note_connect` a few seconds later — observable
    // only by a statistics block that happened to land in the reconnect window,
    // and gone forever otherwise.
    std::uint64_t worst_rtt_ever_us() const noexcept { return worst_rtt_ever_us_; }
    std::uint32_t pings() const noexcept { return pings_; }
    std::uint32_t pongs() const noexcept { return pongs_; }
    std::uint32_t unsolicited() const noexcept { return unsolicited_; }
    std::uint32_t unanswered() const noexcept { return unanswered_; }
    // ATTEMPTS, not established connections, and the name is the weaker of the
    // two on purpose. `note_connect` is called from `WsTransport::open_socket`
    // before DNS/TCP/TLS/upgrade have run, because the state it clears must be
    // gone before any byte of the new socket can reach the probe — so a connect
    // that never completes still counts here. Nothing renders it; it exists so
    // the tests can assert the reset happened.
    std::uint32_t connect_attempts() const noexcept { return connections_; }

    // "rtt 87 ms (worst 111420 ms, run 111420 ms) | ping 42/42 | waiting 31200 ms"
    //
    // Rendered here rather than in the console for the reason the histogram and
    // the age line are: the half of the printing that can be wrong in a way
    // nobody notices is the pairing of a label with the neighbouring field's
    // number, and this is a line a bench evening is read off. The
    // `x (worst y, run z)` shape is `staleness.hpp`'s deliberately — the two
    // lines are printed adjacent and are read as one, so they should not need
    // two different reading habits.
    std::size_t render(std::int64_t now_us, char* out, std::size_t cap) const noexcept {
        if (out == nullptr || cap == 0) { return 0; }
        out[0] = '\0';
        std::size_t at = 0;
        int n;
        if (!kClientPingEnabled) {
            // "We are not asking", which is a different statement from "we asked
            // and nothing came back" — see kClientPingEnabled.
            n = std::snprintf(out, cap, "disabled at build (depthcharge-noping)");
            (void)append_truncating(n, cap, at);
            return at;
        }
        if (!measured()) {
            // NOT AN EARLY RETURN, and that was the bug in the first draft of
            // this function. "Nothing has come back on THIS socket" is not the
            // same as "there is nothing to report": a round-trip that outlived
            // the previous socket lives in `run`, and a ping outstanding right
            // now is the live finding. Returning here printed a shrug over both.
            n = std::snprintf(out, cap, "no round-trip yet");
        } else {
            n = std::snprintf(out, cap, "rtt %llu ms (worst %llu ms, run %llu ms) | ping %u/%u",
                              static_cast<unsigned long long>(last_rtt_us_ / kUsPerMs),
                              static_cast<unsigned long long>(worst_rtt_us_ / kUsPerMs),
                              static_cast<unsigned long long>(worst_rtt_ever_us_ / kUsPerMs),
                              static_cast<unsigned>(pongs_), static_cast<unsigned>(pings_));
        }
        if (append_truncating(n, cap, at)) { return at; }

        // The run-level peak, on the unmeasured branch only — the measured one
        // already carries it inside the parenthesis. Printed whenever it exists,
        // because on a socket that has produced no reading of its own it is the
        // ONLY reading there is.
        if (!measured() && worst_rtt_ever_us_ != 0) {
            n = std::snprintf(out + at, cap - at, " (run %llu ms)",
                              static_cast<unsigned long long>(worst_rtt_ever_us_ / kUsPerMs));
            if (append_truncating(n, cap, at)) { return at; }
        }

        const std::uint64_t out_us = outstanding_us(now_us);
        if (out_us > 0) {
            n = std::snprintf(out + at, cap - at, " | waiting %llu ms",
                              static_cast<unsigned long long>(out_us / kUsPerMs));
            if (append_truncating(n, cap, at)) { return at; }
        }
        // Both are zero on every healthy run, so they are printed only when they
        // are not — a line that always carries two zeros trains the reader to
        // stop looking at it.
        if (unanswered_ != 0 || unsolicited_ != 0) {
            n = std::snprintf(out + at, cap - at, " | lost %u, unsolicited %u",
                              static_cast<unsigned>(unanswered_),
                              static_cast<unsigned>(unsolicited_));
            (void)append_truncating(n, cap, at);
        }
        return at;
    }

private:
    // The instant this probe became free to send again: the connect, or the pong
    // that completed the last round-trip. NOT the last ping sent — measuring the
    // cadence from the send would let a slow round-trip shorten the gap between
    // its own completion and the next ping, which is backwards.
    std::int64_t idle_since_us_ = 0;
    std::int64_t ping_sent_us_ = 0;

    // One place where a round-trip becomes a high-water mark, for the reason
    // StalenessEstimator::bank_peak states and this file inherits: the pair was
    // written out twice — once in note_pong, once in note_disconnect — and a
    // future third caller updating only one of them would produce a `worst` that
    // is quietly smaller than the `run` it is supposed to bound.
    void bank_peak(std::uint64_t rtt) noexcept {
        if (rtt > worst_rtt_us_) { worst_rtt_us_ = rtt; }
        if (rtt > worst_rtt_ever_us_) { worst_rtt_ever_us_ = rtt; }
    }

    std::uint64_t last_rtt_us_ = 0;
    std::uint64_t worst_rtt_us_ = 0;
    std::uint64_t worst_rtt_ever_us_ = 0;

    std::uint32_t pings_ = 0;
    std::uint32_t pongs_ = 0;
    std::uint32_t unsolicited_ = 0;
    std::uint32_t unanswered_ = 0;
    std::uint32_t connections_ = 0;

    bool in_flight_ = false;
};

}  // namespace depthcharge::fw
