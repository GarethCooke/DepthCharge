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
    // When the last byte came off the socket, on the same clock as `now_us`.
    //
    // Zero means "nothing yet", and unlike the two clocks inside this class that
    // is SAFE here rather than the encoding bug the note on those warns about:
    // the silence is measured from `max(last_rx_us, the connect)`, and the
    // connect edge is observed by poll() itself. So a caller that never fills
    // this field in gets a supervisor that simply never recycles, which is the
    // behaviour this class had before the field existed.
    std::int64_t last_rx_us = 0;
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
        // no byte has arrived yet — and what makes `last_rx_us == 0` mean
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
            (in.last_rx_us > socket_up_since_us_) ? in.last_rx_us : socket_up_since_us_;
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
// Arduino's own one-shot retry is fast — on the bench the deauth and the failed
// retry were 28 ms apart — so this only has to be clear of it, not of a full
// association. Five seconds is also longer than the ~2.5 s cold bring-up the
// first bench run measured, so a genuine roam that Arduino IS handling completes
// without us interrupting it.
inline constexpr std::int64_t kWifiRejoinAfterUs = 5 * 1000 * 1000;

// How often we retry once it is ours.
//
// FIVE SECONDS, NOT TEN, AND THE BENCH MOVED IT. The first version used ten on
// the reasoning that an association is scan + auth + assoc + DHCP and the boot
// one measured ~2.5 s, so ten was "a full attempt plus margin". That reasoning
// only considered the attempt. It ignored the term that actually dominates once
// the AP comes back: **the wait for the next attempt to be due.**
//
// The 2026-08-10 run made it visible — the rejoin that finally succeeded was
// #15, and from the AP being unblocked to the panel going live measured 5-10 s,
// of which the connect itself was 4,068 ms and the rest was this constant. Five
// seconds still leaves 2x a measured association, halves the worst-case wait, and
// costs nothing extra against an AP that is deliberately refusing us: a failed
// association is a scan, not a handshake, and there is no handle to burn — which
// is what makes this cheaper to retry than the socket cadence it sits under.
inline constexpr std::int64_t kWifiRejoinCycleUs = 5 * 1000 * 1000;

// Cold Wi-Fi association plus TLS, measured at the stage C bench: 2.5 s. It is a
// strict OVER-estimate of the association alone, which is the quantity that
// matters here, and that is the safe direction.
inline constexpr std::int64_t kObservedAssociationUs = 2500 * 1000;

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
static_assert(kWifiRejoinCycleUs >= kObservedAssociationUs,
              "a rejoin must not preempt an association that could still succeed");

// And the cadence when the last attempt has DEFINITIVELY failed, which is a
// different question and was costing most of the recovery.
//
// kWifiRejoinCycleUs is sized so a retry cannot land on an association that is
// still in flight. But an AP that is refusing us does not leave one in flight —
// the 2026-08-10 bench measured the refusal coming back in **60 ms**:
//
//   23:02:10.419  rejoining (#1)
//   23:02:10.473  Reason: 202 - AUTH_FAIL
//
// So for 4.9 of every 5 s we were waiting out a budget for an attempt that had
// already been over for a moment, and when the AP finally opened we noticed up
// to a full cycle late. That was the single largest tunable term left in the
// recovery: 25.6 s of grey, of which 4,220 ms was the TLS connect (the
// network's, and untouchable from here) and up to 5 s was this.
//
// Arduino latches the failure as WL_CONNECT_FAILED (WiFiGeneric.cpp:1065 on
// AUTH_FAIL, :1088 on ASSOC_FAIL), so "the attempt is over and it lost" is
// observable rather than inferred — the same principle as supervising the socket
// on its state instead of on the library's events. One second is 16x the
// measured refusal, so it cannot outrun a real answer.
inline constexpr std::int64_t kWifiRefusedRetryUs = 1000 * 1000;

static_assert(kWifiRefusedRetryUs < kWifiRejoinCycleUs,
              "the refused-retry path exists to be faster than the in-flight one");

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

    // `attempt_failed` is the station reporting that its last association
    // attempt is over and lost — Arduino's WL_CONNECT_FAILED. It is not the same
    // as "not associated", which is also true while an attempt is running, and
    // the difference is worth up to a whole cycle of grey panel.
    Decision poll(std::int64_t now_us, bool associated, bool attempt_failed = false) noexcept {
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

        // A refused attempt is finished, so there is nothing to preempt and no
        // reason to sit out the in-flight budget. Only after we have actually
        // made an attempt of our own: before that, WL_CONNECT_FAILED is left
        // over from the framework's own retry and says nothing about ours.
        const std::int64_t retry_after =
            (attempt_failed && rejoins_ > 0) ? kWifiRefusedRetryUs : kWifiRejoinCycleUs;
        const std::int64_t due = (rejoins_ == 0) ? down_since_us_ + kWifiRejoinAfterUs
                                                 : last_rejoin_us_ + retry_after;
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
