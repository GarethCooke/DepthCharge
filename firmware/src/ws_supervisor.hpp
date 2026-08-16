// firmware/src/ws_supervisor.hpp — when to stop waiting for a socket, and what
// the reconnect constants have to be for that decision to be safe.
//
// ESP-IDF-free on purpose, like frame_reassembler.hpp, gap_histogram.hpp and
// stall_probe.hpp: this is arithmetic over three inputs (a clock, "is the socket
// up", "is the station associated") and it decides how long the panel stays
// grey. That is exactly the kind of thing that must be argued on the desk with
// ctest rather than by flashing a board and stopping a server by hand — which is
// how every reconnect change before this one was evaluated, at roughly one
// sample per evening.
//
// WsTransport owns the platform half: the socket, the RX task, the DNS warm, the
// logging. This owns the *policy* and the constants.
#pragma once

#include <cstdint>

namespace depthcharge::fw {

// =============================================================================
// Every constant here is now ours, and that is new.
// =============================================================================
//
// This file used to open with two numbers that were not: `kClientWaitTimeoutMs`
// (10 s) and `kClientSelfExitUs` (5 s), read out of the precompiled
// `esp_websocket_client` archive with xtensa objdump because the library's own
// blocking behaviour bounded what a reconnect policy was allowed to do — a
// socket that aborted put its task to sleep for five seconds, and
// `esp_websocket_client_stop()` blocked for whatever was left of that. The
// two-handle design existed to route around it, and a static_assert here kept a
// retry from returning to a handle whose task had not finished dying.
//
// All of that went with the library on 2026-08-16. The transport owns one
// esp-tls connection and `esp_tls_conn_destroy()` returns at once, so there is
// no sleeper to dodge, no spare handle to alternate with, and no reason for this
// file to know anything about a third party's object code. The archaeology is
// preserved in ARCHITECTURE §9 (2026-08-09, 2026-08-10) and in this file's git
// history, where it belongs; what is left below is arithmetic this project can
// change on its own evidence.

// How often the transport is expected to call poll(). Not enforced here — it is
// loopTask's vTaskDelay — but the backoff is stated in terms of it, so it would
// be a lie to leave it implicit.
inline constexpr std::int64_t kSupervisePeriodUs = 250 * 1000;

// How long after the feed dies the first reconnect attempt goes out.
//
// This used to be 2 s, and used to be free: it was spent inside the old
// library's 5 s sleep, so it cost the panel nothing. Nothing sleeps any more, so
// every microsecond of it is grey panel — hence one poll period, the pass after
// the one that noticed.
//
// Not zero. The pass that first sees the socket down only records when it went
// down (there is no earlier timestamp to use), so one period is what the shape
// of the poll loop costs anyway; spelling it as the backoff makes the arithmetic
// in the tests match the arithmetic on the board. The real "don't retry into a
// radio that has not rejoined" protection is the association gate below, which
// is a condition rather than a delay and so costs nothing when it is satisfied.
inline constexpr std::int64_t kReconnectBackoffUs = kSupervisePeriodUs;

// Measured, 2026-08-09 bench: restart -> panel LIVE again, end to end, 6136 ms.
//
// Run C later split that figure — ~2545 ms of it was the blocking stop() this
// design no longer performs, 4018 ms was DNS + TCP + TLS + upgrade, ~435 ms was
// Anvil's snapshot — so the quantity kHandshakeBudgetUs actually has to cover is
// now the 4018 ms, and 6136 ms is a strict over-estimate of it. Kept as the
// anchor of the assert below anyway: it is one sample either way, and the cost
// of being wrong downward is a supervisor that kills connections a beat before
// they succeed, which is the one failure mode here that cannot self-correct.
inline constexpr std::int64_t kObservedRecoveryUs = 6136 * 1000;

// What one connection attempt is allowed to take before the supervisor is
// allowed to give up on it and open the other handle: DNS, TCP, the TLS
// handshake against the pinned root, and the HTTP upgrade.
//
// Understating it is how this code could turn a recoverable outage into a
// permanent one — hence the assert, and hence a value above the worst figure the
// bench has produced rather than the 2.5 s cold bring-up the first run suggested.
inline constexpr std::int64_t kHandshakeBudgetUs = 7 * 1000 * 1000;

static_assert(kHandshakeBudgetUs >= kObservedRecoveryUs,
              "the handshake budget must cover the slowest recovery the bench has measured");

// The interval between the START of one attempt and the start of the next, when
// the first one neither succeeded nor reported anything. Backoff plus budget, so
// that an attempt of ours is never disturbed inside its own budget: asking for a
// fresh socket mid-handshake is the single way this policy could make an outage
// permanent, and it is ruled out on a clock we own.
inline constexpr std::int64_t kRetryCycleUs = kReconnectBackoffUs + kHandshakeBudgetUs;

static_assert(kRetryCycleUs > kHandshakeBudgetUs,
              "supervisor grace must exceed a full client reconnect or it preempts one");

// =============================================================================
// The silence recycle: a socket that is up and saying nothing is a dead socket.
// =============================================================================
//
// THE GAP THIS CLOSES, found live on 2026-08-16 at 00:12. The policy below gates
// everything on `socket_connected`, and a silent-but-open TCP connection reads
// as connected — so the transport would hold it forever. The RX watchdog greys
// the panel honestly within a second, which covers HONESTY; nothing covered
// RECOVERY. On a true half-open (peer gone, no FIN, no RST) the object would sit
// grey behind a "healthy" transport for the rest of the run.
//
// FIVE MINUTES, AND THE NUMBER IS THE WHOLE DESIGN. The first draft of this said
// fifteen seconds — comfortably above Anvil's ~600 ms worst healthy gap and the
// 3.9 s RF fades of 2026-08-13, comfortably below an evening of grey. Four
// minutes later the same night refuted it: Anvil went silent mid-stream for
// **2 min 56 s** and then resumed **on the same TCP connection**, no death, no
// reconnect, the board LIVE the instant bytes flowed. A 15 s recycle would have
// torn down a good connection and paid a reconnect plus a fresh-snapshot cycle
// for nothing.
//
// So the threshold is not sized against weather, it is sized against SERVER
// STALLS, and it sits between the two failure modes with one measurement each:
// a real Anvil stall recovers in ~3 min on the held socket; a true half-open
// never recovers. Five minutes costs a half-open five minutes of grey instead of
// forever, and costs a stall like that one nothing. It moves on evidence — the
// signature to count in a log is a run of `-- rx  wait 99% / 0 reads` windows on
// a live socket.
inline constexpr std::int64_t kSilenceRecycleUs = 5 * 60 * 1000 * 1000LL;

static_assert(kSilenceRecycleUs > 3 * 60 * 1000 * 1000LL,
              "the recycle must outlast the 2m56s Anvil stall measured on 2026-08-16, "
              "or it tears down connections that were about to recover");

// ONE SPELLING OF "THIS SOCKET HAS STOPPED SPEAKING", because there are two
// places that have to agree about it and they run on different cores.
//
// `WsSupervisor::poll()` below decides, on loopTask. `WsTransport::rx_main()`
// acts, on the RX task — and it has to test the condition itself rather than
// trust the request flag, because the flag can arrive stale: supervise() samples
// the socket state and stores the request several lines apart, with a malloc-ing
// log call in between, so a connect that outran `kRetryCycleUs` can land a
// request on a socket that has only just come up. The first version of the
// recycle trusted the flag and would have torn that socket down.
//
// Two call sites comparing against the same constant is how the two drift; one
// `constexpr` function that both call is how they cannot. It is also the whole
// of the recycle's arithmetic, so it is directly host-testable without a board.
constexpr bool socket_is_silent(std::int64_t now_us, std::int64_t alive_since_us) noexcept {
    return (now_us - alive_since_us) >= kSilenceRecycleUs;
}

// =============================================================================
// The policy.
// =============================================================================

enum class SupervisorAction : std::uint8_t {
    None,
    // The socket came up. `elapsed_us` is how long the attempt took, which is
    // the measurement kHandshakeBudgetUs is guessed from — printed on every
    // recovery so the constant stops being a guess.
    ReportConnected,
    // Open the other client handle. `elapsed_us` is how long the feed has been
    // down, `attempt` is the ordinal of the attempt that is now in flight.
    StartAttempt,
};

struct SupervisorDecision {
    SupervisorAction action = SupervisorAction::None;
    std::int64_t elapsed_us = 0;
    std::uint32_t attempt = 0;
    // Set exactly once per outage, on the first poll where an attempt was due
    // and the station was not associated. One line in the log, not one every
    // 250 ms for as long as the Wi-Fi is out.
    bool wifi_holdoff = false;
    // True when the outage this attempt belongs to was opened by silence on a
    // socket that is still up, rather than by the socket going down. The
    // transport needs to know, because the two are different actions over there:
    // one opens a socket, the other has to tear one down first.
    bool silence_recycle = false;
};

struct SupervisorInput {
    std::int64_t now_us = 0;
    bool socket_connected = false;
    // Station associated with an AP and holding an IP. The gate exists because
    // the drops this was written for are the AP steering the board off one mesh
    // node and onto another: the socket dies, and for the next second or two
    // there is no route for DNS or TCP to travel over. An attempt fired into
    // that window cannot succeed, and it is not free — it costs a retry cycle
    // of grey panel.
    bool wifi_associated = false;
    // When the last DATA arrived on the socket, on the same clock as `now_us`.
    //
    // DATA, NOT BYTES, AND THE DISTINCTION IS LOAD-BEARING SINCE THE CLIENT PING
    // WENT ON (ws_ping.hpp). This field used to be fed from every read that
    // produced bytes, which was the same quantity while data frames were all
    // that ever arrived. They are not any more: the board provokes a pong every
    // kPingPeriodUs, and a caller that fed byte-arrival here would refresh this
    // clock with its own control traffic — so a peer that answers pongs and
    // publishes nothing would never be recycled, which is precisely the case
    // this whole mechanism exists for.
    //
    // The rule for any caller, stated because this class cannot enforce it: feed
    // this from something that only a *book event* can advance. Anything a
    // healthy-but-silent peer can produce on its own belongs nowhere near it.
    //
    // Zero means "nothing yet", and unlike the two clocks inside this class that
    // is SAFE here rather than the encoding bug the note on those warns about:
    // the silence is measured from `max(last_data_us, the connect)`, and the
    // connect edge is observed by poll() itself. So a caller that never fills
    // this field in gets a supervisor that simply never recycles, which is the
    // behaviour this class had before the field existed.
    std::int64_t last_data_us = 0;
};

class WsSupervisor {
public:
    // One pass. Call it every kSupervisePeriodUs from a normal task context.
    //
    // The attempt clock is stamped HERE, when StartAttempt is returned, rather
    // than by the caller after the platform call comes back. That ordering used
    // to matter and no longer does: it existed because
    // `esp_websocket_client_stop()` blocked for up to 2.5 s before an attempt
    // could even begin, and letting a slow teardown eat the handshake budget put
    // the next restart on top of the current one. The transport does not call
    // stop() any more, so the only thing between this decision and the socket
    // being built is an xTaskCreate — and stamping early is the conservative
    // direction anyway.
    SupervisorDecision poll(const SupervisorInput& in) noexcept {
        SupervisorDecision out;

        // The socket's own up-edge, observed here rather than trusted from the
        // caller. It is what lets the silence be measured from the connect when
        // nothing has arrived yet — and what makes `last_data_us == 0` mean
        // "unknown" instead of "silent since the epoch".
        if (in.socket_connected) {
            if (!socket_up_) {
                socket_up_ = true;
                socket_up_since_us_ = in.now_us;
            }
        } else {
            socket_up_ = false;
        }

        // The most recent instant this socket is known to have been alive.
        const std::int64_t alive_since_us =
            (in.last_data_us > socket_up_since_us_) ? in.last_data_us : socket_up_since_us_;
        // A socket that is up and has said nothing for kSilenceRecycleUs is
        // treated from here on exactly as a dead one: the outage machinery below
        // is the same code, and the transport's StartAttempt is the same
        // instruction. The only difference reaches the log.
        const bool silent =
            in.socket_connected && socket_is_silent(in.now_us, alive_since_us);

        if (in.socket_connected && !silent) {
            if (attempt_in_flight_) {
                out.action = SupervisorAction::ReportConnected;
                out.elapsed_us = in.now_us - attempt_started_us_;
                out.attempt = attempts_;
                attempt_in_flight_ = false;
            }
            outage_open_ = false;
            wifi_holdoff_reported_ = false;
            return out;
        }

        // First pass of an outage. A socket that went DOWN has no timestamp
        // earlier than this poll, so this pass can only record when it noticed;
        // a socket that went SILENT does have one — the last byte — and using it
        // is what makes `feed down N ms` report the five minutes rather than the
        // poll period. Either way the backoff is measured from the value stored
        // here, so a silence recycle's attempt goes out on the very next pass.
        if (!outage_open_) {
            outage_open_ = true;
            disconnected_since_us_ = silent ? alive_since_us : in.now_us;
            return out;
        }

        // The first attempt of an outage falls a backoff after the feed died;
        // every later one falls a full cycle after the previous attempt STARTED.
        const std::int64_t due = attempt_in_flight_
                                     ? attempt_started_us_ + kRetryCycleUs
                                     : disconnected_since_us_ + kReconnectBackoffUs;
        if (in.now_us < due) { return out; }

        if (!in.wifi_associated) {
            // Held, not skipped: `due` is not advanced, so the attempt goes out
            // on the first pass after the station is back rather than a cycle
            // later. That is the difference between a 4 s grey and an 11 s one
            // on exactly the drop this brief is about.
            if (!wifi_holdoff_reported_) {
                wifi_holdoff_reported_ = true;
                out.wifi_holdoff = true;
            }
            return out;
        }

        out.elapsed_us = in.now_us - disconnected_since_us_;
        note_attempt_begun(in.now_us);
        out.action = SupervisorAction::StartAttempt;
        out.attempt = attempts_;
        // `silent`, not "this outage began in silence". The two differ from the
        // second attempt onward: once the transport has torn the socket down the
        // outage is still open, but there is no longer a live socket to recycle,
        // and a retry labelled as one would print `socket up but silent` over a
        // socket that is definitively gone. What the transport is asking is
        // "must I drop something first", and only the live reading answers it.
        out.silence_recycle = silent;
        return out;
    }

    // The boot connection is attempt #1, and marking it is not bookkeeping — it
    // is what buys it the same handshake immunity every retry gets. Without it
    // the first poll sees a client that has simply never been connected, cannot
    // tell that from one that dropped, and opens the spare a backoff in: straight
    // through the cold TLS handshake, which the 2026-08-09 18:15 bench caught it
    // doing and which read as noise because it recovered.
    void note_attempt_begun(std::int64_t now_us) noexcept {
        ++attempts_;
        attempt_started_us_ = now_us;
        attempt_in_flight_ = true;
    }

    std::uint32_t attempts() const noexcept { return attempts_; }
    bool attempt_in_flight() const noexcept { return attempt_in_flight_; }

private:
    // Two clocks, and the whole correctness argument is the gap between them.
    // `disconnected_since_us_` says when the feed died and schedules the FIRST
    // attempt a backoff later. `attempt_started_us_` says when the last attempt
    // began and buys it kHandshakeBudgetUs of immunity, inside which poll() does
    // nothing at all.
    //
    // Each carries its own validity flag rather than reserving 0 for "unset",
    // and that is not tidiness. The old code used the 0 sentinel, and the boot
    // connection is stamped from `esp_timer_get_time()` — a clock that reads 0
    // at reset. On the board the call lands a second or two in, so it never bit;
    // on the desk the first test written against this file produced `t = 0`,
    // read it as "no attempt in flight" and opened a second connection straight
    // through the cold TLS handshake — the exact bug the boot stamp was added to
    // fix, re-entering by the back door of an encoding.
    std::int64_t disconnected_since_us_ = 0;
    std::int64_t attempt_started_us_ = 0;
    std::uint32_t attempts_ = 0;
    bool outage_open_ = false;
    bool attempt_in_flight_ = false;
    bool wifi_holdoff_reported_ = false;

    // The silence half. `socket_up_since_us_` carries the same validity-flag
    // discipline as the two clocks above and for the same reason: it is only
    // ever read when `socket_up_` says an up-edge has been seen on this run, so
    // a board whose first connect lands at t = 0 cannot be read as a socket that
    // has been silent forever.
    std::int64_t socket_up_since_us_ = 0;
    bool socket_up_ = false;
};

// =============================================================================
// The association, which nothing was supervising at all.
// =============================================================================
//
// WsSupervisor above holds an attempt while the station is down and waits for it
// to come back. The 2026-08-10 bench found the case where it never does, and the
// panel stayed grey for the rest of the run:
//
//   Reason: 1 - UNSPECIFIED      the AP deauthenticates (a Deco block)
//   Reason: 202 - AUTH_FAIL      Arduino's one retry, rejected
//   reconnect due but the station is not associated — holding
//   ... nothing, forever. connects=2, rate 0.00/s, drew 0.
//
// THE CAUSE IS IN THE FRAMEWORK, READ OUT OF THE SHIPPED SOURCE.
// `WiFiGeneric.cpp:1083` only calls `WiFi.begin()` again when
// `_isReconnectableReason(reason)` is true, and that list (line 1177) contains
// `WIFI_REASON_802_1X_AUTH_FAILED` but NOT `WIFI_REASON_AUTH_FAIL` (202). The
// deauth reason 1 IS reconnectable, so Arduino retries exactly once; the AP is
// still blocking, so that attempt returns 202; `first_connect` is already false;
// `DoReconnect` stays false. Nothing calls `WiFi.begin()` ever again.
//
// So `connect_wifi()` being a one-shot at boot was a latent hole, and this is
// the same lesson the reconnect rework already produced, applied one layer down:
// SUPERVISE ON OBSERVED STATE, NOT ON THE LIBRARY'S POLICY. The observed state
// here is `WiFi.status() == WL_CONNECTED`, and if it has been false for long
// enough we stop waiting for Arduino and rejoin ourselves.
//
// Eighth time this precompiled vintage has decided a design, after
// esp_crt_bundle, heap_trace_start, reconnect_timeout_ms,
// CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS, the blocking stop(), the
// self-leaking setupDMA() and the S3's non-blocking buffer flip.

// How long the station may be down before we take the rejoin over.
//
// TEN SECONDS, AND IT IS THE SAME BOUND AS THE CYCLE BELOW FOR THE SAME REASON.
// This was five, on the reasoning that "Arduino's own one-shot retry is fast —
// on the bench the deauth and the failed retry were 28 ms apart — so this only
// has to be clear of it, not of a full association."
//
// Both halves of that were wrong, and the 2026-08-16 log says so directly:
//
//   08:57:39.362  Reason: 1 - UNSPECIFIED    the deauth
//   08:57:42.230  Reason: 202 - AUTH_FAIL    Arduino's retry, 2,868 ms later
//
// 2,868 ms, not 28. And that is the REFUSED case; if the AP accepts, Arduino's
// retry is a full association and takes ~4 s. Taking over at five seconds would
// then abort an attempt that was about to succeed — the identical defect the
// post-mortem below records for the cycle, sitting in its sibling constant with
// no assert on it at all.
//
// So it clears a full association with the same margin the cycle does. The cost
// is five extra seconds before we start trying, and only in the case where
// Arduino has genuinely given up — if it has not, the station comes back on its
// own and this constant is never reached.
inline constexpr std::int64_t kWifiRejoinAfterUs = 10 * 1000 * 1000;

// One association attempt, MEASURED rather than estimated — 2026-08-16, on the
// board, from the log that convicted the constant below it: `WiFi.begin()` at
// uptime 3,786 ms, `wifi up` with an IP at 7,821 ms. **4,035 ms.**
//
// The previous value was 2,500 ms and described itself as "a strict
// OVER-estimate of the association alone". It was neither: it came from a stage
// C bench figure for a cold bring-up on a quiet channel, and every association
// this project has actually timed since has been longer — 4,068 ms on
// 2026-08-10, 4,035 ms on 2026-08-16, and that is with the BSSID already chosen.
// The supervisor's rejoin path calls a plain `WiFi.begin(ssid, password)`, which
// runs its own all-channel scan first (~3 s on this desk), so the true figure on
// that path is larger again and this remains the conservative floor rather than
// the ceiling.
inline constexpr std::int64_t kObservedAssociationUs = 4035 * 1000;

// How often we retry once it is ours.
//
// TEN SECONDS, AND THE BENCH MOVED IT BACK. This was ten, then five from
// 2026-08-10, and the 2026-08-16 pull-the-Wi-Fi test says five was never safe:
// see the deleted fast path's post-mortem below. A rejoin BEGINS WITH
// `WiFi.disconnect()`, so every retry destroys the association attempt the
// previous one started; the cadence is therefore not a latency knob that can be
// tuned freely, it is a hard lower bound set by how long an attempt needs.
//
// Ten is 2.5x the measured 4,035 ms, and the margin is deliberate rather than
// round: the rejoin path's `begin()` does its own all-channel scan on top of the
// association, and that scan is not in the 4,035 ms figure.
//
// What this costs, stated plainly because it is a real regression against the
// 2026-08-10 tuning: up to ten seconds of extra grey panel between the AP
// accepting us again and the board noticing. What the five-second version cost
// was **never noticing at all**, which is not a trade.
inline constexpr std::int64_t kWifiRejoinCycleUs = 10 * 1000 * 1000;

// The real constraint on the retry cadence, and it took the assert firing to
// state it correctly. The first version asserted
// `kWifiRejoinCycleUs > kWifiRejoinAfterUs`, which is meaningless: the two are
// measured from different instants — the threshold from when the station went
// down, the cycle from the last rejoin — and neither bounds the other. Dropping
// the cycle to 5 s made them equal and the assert failed, which is the only
// reason anyone looked at what it was claiming.
//
// What must hold is that a retry does not land on top of an association that is
// still in flight, exactly as kRetryCycleUs must not preempt a handshake above.
// It now demands a FACTOR rather than a bare inequality, because the 2026-08-10
// version was satisfied with zero margin (5 s cycle, 2.5 s association) by a
// constant that turned out to be an under-estimate — an assert that passes at
// exactly 1.0x is an assert that is one bad measurement from meaning nothing.
static_assert(kWifiRejoinCycleUs >= 2 * kObservedAssociationUs,
              "a rejoin must not preempt an association that could still succeed, "
              "and must keep margin over the measured one");

// BOTH instants that can fire a rejoin, not just the repeating one. The 2026-08
// -10 version asserted only the cycle, and the first takeover — a different
// clock, measured from when the station went down rather than from the last
// attempt — was left to a comment claiming Arduino answers in 28 ms. It answers
// in 2,868 ms, and if the AP accepts it does not answer at all until the
// association completes. Every path that calls WiFi.disconnect() has to clear an
// association, so every constant that schedules one is asserted here.
static_assert(kWifiRejoinAfterUs >= 2 * kObservedAssociationUs,
              "the first takeover must not preempt the association Arduino's own "
              "retry may still be running");

// =============================================================================
// THE REFUSED-RETRY FAST PATH IS DELETED. Post-mortem, 2026-08-16.
// =============================================================================
//
// There used to be a `kWifiRefusedRetryUs` of 1 s here, taken whenever Arduino
// had latched WL_CONNECT_FAILED. Its argument was good and its measurement was
// real: an AP that is refusing answers in 60 ms (2026-08-10 — `rejoining (#1)`
// at 23:02:10.419, `Reason: 202 - AUTH_FAIL` at 23:02:10.473), so waiting out a
// full cycle before trying again meant noticing that it had STOPPED refusing up
// to a cycle late, and that was the largest tunable term left in a 25.6 s grey.
//
// **It livelocked the board, and the pull-the-Wi-Fi acceptance found it.**
// 2026-08-16, Deco blocking the station: **388 rejoin calls produced TWO
// AUTH_FAIL responses from the AP** (08:57:42 and 08:59:42). If each call had
// been an attempt the AP refused there would have been ~388 of them, 60 ms
// apart. They never reached the AP: a rejoin begins with `WiFi.disconnect()`,
// an association needs ~4 s, and a 1 s retry destroys the attempt in flight
// every single time. WL_CONNECT_FAILED is STICKY — nothing clears it while no
// attempt is ever allowed to resolve — so the fast path latched on and the
// station could never re-associate. The board sat honestly grey for the rest of
// the run, and the only thing that recovered it was a power cycle (`rst:0x1
// (POWERON)` in the log, four seconds before the association that "worked",
// because `connect_wifi()` runs once at boot and nothing interrupts it).
//
// The rule this cost, and it is the fifth time this milestone has paid for a
// version of it: **a measurement of the failing case does not bound the
// succeeding case.** 60 ms was the time to be REFUSED. The quantity the retry
// cadence has to respect is the time to be ACCEPTED, which is 67x larger, and
// the fast path was sized against the one that could be measured while the
// thing it existed to detect was the other one. The `static_assert` above
// already stated the correct rule; the fast path simply routed around the
// constant it guards.
//
// A safe fast path would need to distinguish "refused just now, by our attempt"
// from "refused two minutes ago and still latched", and WL_CONNECT_FAILED
// carries no timing to do it with. If a future session wants the recovery
// latency back, the lever is not this flag — it is making the rejoin cheaper
// (an explicit scan-then-join like `connect_wifi()`'s, moved off loopTask so it
// can block), not making it more frequent.
//
// The `attempt_failed` parameter went with it, and a counter that was drafted to
// replace it went too — worth recording, because the counter looked useful. The
// idea was to expose refusals beside rejoins so the 388-vs-2 ratio would be one
// glance on the stats block instead of a log diff. It cannot work from this
// signal: WL_CONNECT_FAILED is sticky and sampled at 250 ms, so in the BROKEN
// case the flag never falls and the counter reads one, and in the HEALTHY case
// the 60 ms refusal is back before the next poll and it reads one as well. An
// instrument that gives the same answer in both worlds is not a weak instrument,
// it is a misleading one. **The signal that actually diagnosed this is already
// free**: Arduino's own `Reason: 202 - AUTH_FAIL` lines, which are event-driven
// and cannot be missed. If a board-side counter is ever wanted, it has to hang
// off `WiFi.onEvent(ARDUINO_EVENT_WIFI_STA_DISCONNECTED)`, not off a polled
// status word.

class WifiSupervisor {
public:
    struct Decision {
        // Call WiFi.disconnect() then WiFi.begin(ssid, password).
        bool rejoin = false;
        // How long the station has been down, for the log line. The number is
        // the point: an association that comes back on its own never reaches
        // kWifiRejoinAfterUs, so a non-empty rejoin log IS the evidence that
        // the framework gave up.
        std::int64_t down_us = 0;
        std::uint32_t attempt = 0;
    };

    // Two inputs and no third, which is the shape this had before 2026-08-10 and
    // has again: a clock, and whether the station is associated. Nothing the
    // framework reports about WHY it is not associated may change the cadence —
    // the post-mortem above is what that cost.
    Decision poll(std::int64_t now_us, bool associated) noexcept {
        Decision out;

        if (associated) {
            down_open_ = false;
            rejoins_ = 0;
            return out;
        }

        // Same shape as the outage clock above: the first pass that sees the
        // station down has no earlier timestamp than its own.
        if (!down_open_) {
            down_open_ = true;
            down_since_us_ = now_us;
            return out;
        }

        // ONE CADENCE, WHATEVER THE STATION SAYS. Every rejoin destroys the
        // attempt the last one started, so the interval is a floor set by how
        // long an attempt needs and not a knob. The deleted fast path is the
        // post-mortem above.
        const std::int64_t due = (rejoins_ == 0) ? down_since_us_ + kWifiRejoinAfterUs
                                                 : last_rejoin_us_ + kWifiRejoinCycleUs;
        if (now_us < due) { return out; }

        ++rejoins_;
        last_rejoin_us_ = now_us;
        out.rejoin = true;
        out.down_us = now_us - down_since_us_;
        out.attempt = rejoins_;
        return out;
    }

    std::uint32_t rejoins() const noexcept { return rejoins_; }

private:
    std::int64_t down_since_us_ = 0;
    std::int64_t last_rejoin_us_ = 0;
    std::uint32_t rejoins_ = 0;
    bool down_open_ = false;
};

}  // namespace depthcharge::fw
