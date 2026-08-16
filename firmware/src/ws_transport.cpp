// firmware/src/ws_transport.cpp — see ws_transport.hpp.
#include "ws_transport.hpp"

#include <WiFi.h>

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"

#include <strings.h>   // strncasecmp, for the upgrade's header check

#include "lwip/sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"

#include "anvil_root_ca.hpp"

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "ws";

// How long esp_tls may spend on DNS + TCP + the handshake before it gives up.
// The bench's worst measured connect is 4,220 ms, and kHandshakeBudgetUs (7 s)
// is what the supervisor allows an attempt — so this sits between them: long
// enough never to abandon a connect that would have succeeded, short enough that
// esp-tls returns before the supervisor would have wanted to try again.
constexpr std::uint32_t kConnectTimeoutMs = 6000;

// How long the 101 may take to arrive once the request is written.
constexpr std::int64_t kUpgradeBudgetUs = 5 * 1000 * 1000;

// How long a single write may take before the socket is treated as dead. A WALL
// CLOCK, not a stall count, and the 2026-08-15 review is why: the first version
// counted 100 WANT_WRITE stalls at 10 ms apart and called that "a second" — but
// each esp_tls_conn_write can itself block up to the 5 s SO_SNDTIMEO before
// reporting the stall, so the "bounded" loop's true worst case was ~8.4 minutes
// of the RX task held hostage by a wedged peer, with the supervisor unable to
// help because this IS the task it asks for sockets. Two seconds is still an
// age for a 6-byte pong; one in-flight SO_SNDTIMEO can extend the true bound by
// up to 5 s, which is accepted and stated rather than hidden.
constexpr std::int64_t kWriteBudgetUs = 2 * 1000 * 1000;

// Case-insensitive header lookup over a NUL-terminated header block. HTTP field
// names are case-insensitive (RFC 9110 §5.1) and servers differ; a check that
// only matched one spelling would silently degrade to no check at all.
//
// Returns true only if the field is present AND begins with `want`. Present and
// different is false, which is the case that matters: it means something
// answered the upgrade without doing the SHA1.
bool header_begins_with(const char* headers, const char* name, const char* want) noexcept {
    const std::size_t name_len = std::strlen(name);
    const std::size_t want_len = std::strlen(want);
    for (const char* line = headers; line != nullptr && *line != '\0';) {
        const char* eol = std::strstr(line, "\r\n");
        const std::size_t len = (eol != nullptr) ? static_cast<std::size_t>(eol - line)
                                                 : std::strlen(line);
        if (len > name_len && strncasecmp(line, name, name_len) == 0) {
            const char* v = line + name_len;
            const char* end = line + len;
            while (v < end && (*v == ' ' || *v == '\t')) { ++v; }
            return static_cast<std::size_t>(end - v) >= want_len &&
                   std::strncmp(v, want, want_len) == 0;
        }
        line = (eol != nullptr) ? eol + 2 : nullptr;
    }
    return false;
}
}  // namespace

bool WsTransport::pick_strongest(const char* ssid, std::uint8_t (&bssid)[6],
                                 std::int32_t& channel) noexcept {
    const std::uint32_t t0 = millis();
    const std::int16_t n = WiFi.scanNetworks();
    if (n <= 0) {
        ESP_LOGW(kTag, "wifi: scan returned %d after %u ms — letting the driver choose",
                 static_cast<int>(n), static_cast<unsigned>(millis() - t0));
        WiFi.scanDelete();
        return false;
    }

    // EVERY sibling is printed, not just the winner, and that is the point: the
    // acceptance check is "did this boot join the strongest node IT could see",
    // which is unanswerable from a log that only records the choice. It is also
    // the co-channel picture for free — three Decos on channel 4 is the
    // 2026-08-13 desk, and worth seeing when the numbers stop making sense.
    std::int32_t best_rssi = 0;
    std::uint32_t siblings = 0;
    for (std::int16_t i = 0; i < n; ++i) {
        if (WiFi.SSID(i) != ssid) { continue; }
        const std::uint8_t* b = WiFi.BSSID(i);
        const std::int32_t rssi = WiFi.RSSI(i);
        const std::int32_t ch = WiFi.channel(i);
        ESP_LOGI(kTag, "wifi: sibling %02X:%02X:%02X:%02X:%02X:%02X ch %d rssi %d dBm", b[0], b[1],
                 b[2], b[3], b[4], b[5], static_cast<int>(ch), static_cast<int>(rssi));
        if (siblings == 0 || rssi > best_rssi) {
            std::memcpy(bssid, b, sizeof(bssid));
            channel = ch;
            best_rssi = rssi;
        }
        ++siblings;
    }
    WiFi.scanDelete();

    if (siblings == 0) {
        ESP_LOGW(kTag, "wifi: %d networks visible, none of them ours — letting the driver choose",
                 static_cast<int>(n));
        return false;
    }
    ESP_LOGI(kTag, "wifi: joining %02X:%02X:%02X:%02X:%02X:%02X ch %d at %d dBm — strongest of %u"
                   " siblings, scan took %u ms",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
             static_cast<int>(channel), static_cast<int>(best_rssi),
             static_cast<unsigned>(siblings), static_cast<unsigned>(millis() - t0));
    return true;
}

bool WsTransport::connect_wifi(const char* ssid, const char* password,
                               std::uint32_t timeout_ms) noexcept {
    // Kept so supervise() can rejoin later. This function used to be the ONLY
    // place the station was ever asked to associate, which is the hole the
    // 2026-08-10 bench fell into — see WifiSupervisor.
    ssid_ = ssid;
    password_ = password;

    WiFi.mode(WIFI_STA);
    // Scan every channel and join by SIGNAL, not by whoever answers first — the
    // framework default is WIFI_FAST_SCAN, first probe response wins, which on a
    // mesh where every node broadcasts the same SSID is a lottery.
    //
    // THESE TWO LINES ARE NO LONGER THE FIX; they are belt and braces. They were
    // the fix on 2026-08-13 and failed their five-boot acceptance the same
    // evening — two of five boots landed on weak siblings with both settings
    // demonstrably active (`hardware/bench-2026-08-13-wifi-drop-diagnosis.md`,
    // closing section). The boot join is now explicit (pick_strongest below);
    // what these still cover is every path that reaches a plain WiFi.begin(),
    // which is the supervisor's rejoin, where a blocking scan is not affordable.
    // Ordered before begin() like setSleep(), and for the same reason: applied
    // at STA start, so it must be cached before the association is queued.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    // Modem sleep off by default. The power-save mode parks the radio between
    // DTIM beacons and can add hundreds of milliseconds of RX latency — which on
    // a feed whose worst healthy inter-frame gap is 391-594 ms against a 1000 ms
    // watchdog would manufacture outages that never happened. This is a
    // mains-powered desk object; the power cost is irrelevant and the latency is
    // not.
    //
    // Called AFTER WiFi.mode() and BEFORE WiFi.begin(), which is the order that
    // makes it stick, and that is a claim from the shipped source rather than
    // from habit: `WiFiGenericClass::setSleep` only forwards to
    // `esp_wifi_set_ps` when `getMode() & WIFI_MODE_STA` is already set, so
    // calling it first would cache the preference and never apply it; and the
    // Arduino event handler re-applies the cached value on
    // ARDUINO_EVENT_WIFI_STA_START, so association cannot undo it either.
    WiFi.setSleep(kWifiPowerSave);

    // THE JOIN IS OURS, not the driver's. The two calls above were the
    // 2026-08-13 fix and they failed their own five-boot acceptance that
    // evening; pick_strongest() surveys and names the node instead, and the
    // survey it prints is the acceptance instrument. Falling back to a plain
    // begin() when the scan finds nothing is deliberate — a board that cannot
    // see its own SSID has a bigger problem than which sibling it picks, and
    // refusing to try would turn a bad scan into a dead panel.
    std::uint8_t bssid[6] = {};
    std::int32_t channel = 0;
    if (pick_strongest(ssid, bssid, channel)) {
        WiFi.begin(ssid, password, channel, bssid);
    } else {
        WiFi.begin(ssid, password);
    }

    const std::uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - started > timeout_ms) {
            ESP_LOGE(kTag, "wifi: no association after %u ms (status %d)",
                     static_cast<unsigned>(timeout_ms), static_cast<int>(WiFi.status()));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Read the mode back OUT OF THE DRIVER rather than reporting what we asked
    // for. `WiFi.getSleep()` would only return Arduino's cached preference, and
    // the whole value of this line is that it cannot agree with us by
    // construction: if a bench run shows arrival-side holes with `ps=0` printed
    // here, power save is excluded as a candidate on evidence rather than on the
    // presence of a `setSleep` call three lines up.
    wifi_ps_type_t ps = WIFI_PS_NONE;
    const esp_err_t ps_err = esp_wifi_get_ps(&ps);
    // bssid= names WHICH mesh node this association landed on — every Deco
    // broadcasts the same SSID, so rssi without bssid is unattributable, and
    // the 2026-08-13 storm was undiagnosable for exactly that reason: −78 dBm
    // could not say "far node" without the node's address beside it. Boot-time
    // only; a mid-run re-association is not reprinted here.
    ESP_LOGI(kTag, "wifi up: ip=%s bssid=%s ch=%d rssi=%d dBm | power-save requested=%s"
                   " driver ps=%d%s (0=NONE 1=MIN_MODEM 2=MAX_MODEM)",
             WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str(),
             static_cast<int>(WiFi.channel()), static_cast<int>(WiFi.RSSI()),
             kWifiPowerSave ? "ON" : "OFF", static_cast<int>(ps),
             (ps_err == ESP_OK) ? "" : " (read failed)");
    return true;
}

std::int64_t WsTransport::warm_dns() noexcept {
    const std::int64_t t0 = esp_timer_get_time();

    addrinfo hints = {};
    hints.ai_family = AF_INET;  // the client connects over IPv4; asking for both
                                // would time a lookup the socket never makes
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(kAnvilHost, kAnvilPortText, &hints, &res);
    const std::int64_t elapsed = esp_timer_get_time() - t0;
    if (res != nullptr) { ::freeaddrinfo(res); }

    if (rc != 0) {
        // Not fatal and not a reason to skip the attempt: the client resolves
        // again itself, and a resolver that failed here may succeed a beat
        // later. Logged because a reconnect that fails with `dns=fail` in front
        // of it is a different bug from one that fails after a good lookup.
        ESP_LOGW(kTag, "dns: %s did not resolve (rc %d) after %d ms", kAnvilHost, rc,
                 static_cast<int>(elapsed / 1000));
    }
    return elapsed;
}

void WsTransport::sample_rssi(std::int64_t now) noexcept {
    if (now - last_rssi_us_ < kRssiPeriodUs) { return; }
    last_rssi_us_ = now;
    // esp_wifi_sta_get_ap_info rather than WiFi.RSSI(): the Arduino wrapper
    // returns 0 both for "not associated" and for a genuine 0 dBm, and this is
    // an instrument whose whole job is to be checkable against a link verdict.
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        link_.note(static_cast<std::int8_t>(ap.rssi));
    }
}

void WsTransport::supervise() noexcept {
    if (rx_task_ == nullptr) { return; }

    const std::int64_t now = esp_timer_get_time();
    // Sampled on every pass, connected or not, and before anything below — the
    // rssi through a hole is exactly the number a link verdict has to be checked
    // against, so it must not stop being collected precisely when the transport
    // is unhappy.
    sample_rssi(now);

    SupervisorInput in;
    in.now_us = now;
    in.socket_connected = connected();
    // The silence recycle's input. Written by the RX task at the connect and at
    // every read that produced bytes, so "socket up and this is old" is the
    // whole of the half-open detector — no client ping, no server cooperation.
    in.last_rx_us = last_rx_us_.load(std::memory_order_relaxed);
    // WiFi.status(), not esp_wifi_sta_get_ap_info(): the question this gate asks
    // is "could a TCP connection possibly succeed", and that needs an IP, which
    // is what Arduino's status tracks and an AP record does not.
    in.wifi_associated = (WiFi.status() == WL_CONNECTED);

    // THE ASSOCIATION, BEFORE THE SOCKET. Arduino gives up permanently on an
    // AUTH_FAIL deauth (ws_supervisor.hpp cites the framework lines), so if
    // nothing here rejoins, the socket supervisor below holds forever and the
    // panel greys for the rest of the run — which is exactly what the bench saw.
    //
    // WiFi.begin() is non-blocking: it queues the association and returns, so
    // this costs loopTask nothing and cannot stall the 250 ms poll. The
    // disconnect() first is what clears WL_CONNECT_FAILED, which the framework
    // latches on AUTH_FAIL and which begin() alone does not reset.
    // WL_CONNECT_FAILED used to be read here and passed to the supervisor, where
    // it bought a one-second retry instead of the full cycle. It is not read any
    // more, and the post-mortem in ws_supervisor.hpp is why: the flag is sticky,
    // so on 2026-08-16 it latched for a whole outage and the fast path it
    // unlocked aborted every association before it could complete — 388 rejoins,
    // two answers from the AP, and the board only came back on a power cycle.
    //
    // The diagnostic that replaces it costs nothing and cannot be missed:
    // Arduino's own `Reason: NNN` lines, which this build already prints because
    // CORE_DEBUG_LEVEL is 3. Count those against the `rejoining (#N)` lines below
    // and a livelock is one grep.
    if (const auto w = wifi_supervisor_.poll(now, in.wifi_associated); w.rejoin) {
        ESP_LOGW(kTag, "wifi down %d ms and the framework has stopped trying — rejoining (#%u)",
                 static_cast<int>(w.down_us / 1000), static_cast<unsigned>(w.attempt));
        WiFi.disconnect();
        if (ssid_ != nullptr) {
            WiFi.begin(ssid_, password_);
        } else {
            // Only reachable if supervise() ran before connect_wifi(), which
            // main.cpp's bring-up order rules out. Reported rather than silently
            // skipped, because a rejoin that does nothing looks identical in the
            // log to one that failed.
            ESP_LOGE(kTag, "no credentials to rejoin with — connect_wifi() never ran");
        }
    }

    const SupervisorDecision d = supervisor_.poll(in);

    if (d.wifi_holdoff) {
        ESP_LOGW(kTag, "reconnect due but the station is not associated — holding");
    }

    switch (d.action) {
        case SupervisorAction::ReportConnected:
            // The measurement kHandshakeBudgetUs is guessed from, printed on
            // every recovery so the constant stops being a guess. This is the
            // SOCKET half only; the panel prints its own "grey for N ms", and
            // the difference between the two numbers is how long Anvil took to
            // send the snapshot that grants LIVE.
            ESP_LOGI(kTag, "socket up, %d ms into attempt #%u",
                     static_cast<int>(d.elapsed_us / 1000), static_cast<unsigned>(d.attempt));
            break;

        case SupervisorAction::StartAttempt:
            // The decision is loopTask's; the work is the RX task's. Everything
            // that used to happen here — the DNS warm, esp_websocket_client_start
            // — blocked this task for as long as the network felt like it, and
            // this is now a single store. The attempt clock has already been
            // stamped inside the policy, so the ~4 s the RX task is about to
            // spend inside esp_tls_conn_new_sync is charged to the attempt that
            // asked for it rather than to the one after.
            //
            // Two log lines, because they are two different events and a bench
            // reading one as the other would be reading a healthy socket being
            // recycled as a socket that died. `silence_recycle` means the socket
            // is STILL UP and the RX task has to tear it down before it can open
            // the replacement — see rx_main()'s timeout branch, which is the only
            // place that can, because it is the task holding the socket.
            if (d.silence_recycle) {
                ESP_LOGW(kTag,
                         "socket up but silent for %d s — recycling it (attempt #%u); a wedged"
                         " peer that never sends a FIN looks exactly like this",
                         static_cast<int>(d.elapsed_us / 1000000),
                         static_cast<unsigned>(d.attempt));
            } else {
                ESP_LOGW(kTag, "feed down %d ms — asking the RX task for a socket (attempt #%u)",
                         static_cast<int>(d.elapsed_us / 1000),
                         static_cast<unsigned>(d.attempt));
            }
            connect_requested_.store(true, std::memory_order_relaxed);
            break;

        case SupervisorAction::None:
            break;
    }
}

bool WsTransport::start() noexcept {
    if (rx_task_ != nullptr) { return true; }

    // Pinned to Core 0, which is the whole of the difference from the old
    // client's task and could not be asked of it: this vintage's
    // esp_websocket_client_config_t has no task_core_id, so its task ran
    // wherever the scheduler put it — sometimes on Core 1 beside the panel's
    // render task, sometimes not, and never the same across two bench runs.
    const BaseType_t ok =
        xTaskCreatePinnedToCore(&WsTransport::rx_trampoline, "dc_ws", kRxTaskStack, this,
                                kRxTaskPriority, &rx_task_, kRxTaskCore);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "could not create the RX task");
        rx_task_ = nullptr;
        return false;
    }

    // The boot connection is attempt #1, and marking it is not bookkeeping — it
    // is what buys it the same handshake immunity every retry gets. Without this
    // the supervisor sees a client that has simply never been connected, cannot
    // tell that from one that dropped, and asks for another socket a backoff in:
    // straight through the cold TLS handshake, which the 2026-08-09 18:15 log
    // caught the old client doing.
    supervisor_.note_attempt_begun(esp_timer_get_time());
    connect_requested_.store(true, std::memory_order_relaxed);
    // Composed here rather than stored as a sixth endpoint constant — see the
    // note above kAnvilHost. This is the only place the `wss://` spelling exists.
    ESP_LOGI(kTag, "connecting to wss://%s%s — owned client, RX task on core %d prio %d",
             kAnvilHost, kAnvilPath, static_cast<int>(kRxTaskCore),
             static_cast<int>(kRxTaskPriority));
    return true;
}

void WsTransport::rx_trampoline(void* arg) noexcept { static_cast<WsTransport*>(arg)->rx_main(); }

void WsTransport::rx_main() noexcept {
    for (;;) {
        if (tls_ == nullptr) {
            // Nothing to read and nothing to decide: the supervisor owns WHEN,
            // exactly as it did when this was esp_websocket_client_start() on
            // loopTask. 20 ms is a poll of a flag, an order of magnitude finer
            // than the 250 ms cadence that sets it, so it adds nothing
            // measurable to a recovery.
            if (connect_requested_.exchange(false, std::memory_order_relaxed)) {
                (void)open_socket();
            } else {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }

        const std::int64_t read_began_us = esp_timer_get_time();
        const int rc = static_cast<int>(esp_tls_conn_read(tls_, rx_buf_, sizeof(rx_buf_)));
        // Read before anything else can touch it. newlib's errno is per-task and
        // this is the failing call's own task, so this is the strongest version
        // of the datum the old client could only approximate — its handler ran
        // after abort_connection() had already closed the transport, and either
        // the close_notify write or the close(fd) could overwrite the value.
        const int read_errno = errno;
        // THE ARRIVAL STAMP, and it is now as far upstream as this firmware can
        // possibly reach: the instruction after the read that produced the
        // bytes. It used to be taken inside esp_websocket_client's callback,
        // which is already downstream of an esp_event dispatch hop — so the
        // arrival-vs-event split (strain 12) got sharper for free, and a hole on
        // the arrival side can no longer be that library's scheduling.
        // It doubles as the budget's read stopwatch (rx_budget.hpp): the same
        // two stamps that bound the read bound its cost.
        const std::int64_t at_us = esp_timer_get_time();

        if (rc > 0) {
            budget_.on_read(at_us - read_began_us, static_cast<std::uint32_t>(rc));
            socket_bytes_ += static_cast<std::uint64_t>(rc);
            // The silence recycle's clock, reset by the only thing that proves
            // the socket is alive: bytes off it. Stored before the parse, so a
            // read that costs an unusually long feed cannot age its own arrival.
            last_rx_us_.store(at_us, std::memory_order_relaxed);
            parser_.feed(rx_buf_, static_cast<std::uint32_t>(rc), at_us);
            budget_.on_feed(esp_timer_get_time() - at_us);
            if (parser_.failed()) {
                // The stream is no longer a WebSocket stream. A desynced parser
                // cannot resynchronise by reading further, so the socket goes.
                die(ws_error_name(parser_.error()), 0, 0);
                continue;
            }
            if (close_code_ >= 0) {   // the server said goodbye in-band
                die("ws-close-frame", 0, 0);
                continue;
            }
            if (!maybe_ping(at_us)) { continue; }
        } else if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // SO_RCVTIMEO expired. NOT a death: Anvil's worst healthy gap is
            // ~600 ms and the 2026-08-13 fade record has measured silences to
            // 3.9 s on a weak association that never dropped a socket. What
            // greys the panel for silence is the feed task's 1 s RX watchdog,
            // which is a statement about DATA and belongs there.
            budget_.on_wait(at_us - read_began_us);

            // THE SILENCE RECYCLE'S PLATFORM HALF, and this is the only branch
            // it can live in: a socket that has stopped speaking takes exactly
            // this path, once a second, forever.
            //
            // BOTH HALVES OF THE PREDICATE, and the second one is not
            // belt-and-braces. The first draft tore the socket down on the flag
            // alone, reasoning that a `connect_requested_` seen while `tls_` is
            // non-null could only have come from the silence rule. It cannot:
            // supervise() samples `connected()` and stores the flag several
            // lines apart, with an ESP_LOGW that mallocs and takes the UART
            // mutex in between, and it runs on loopTask on the other core. A
            // connect that outran kRetryCycleUs (7.25 s — the two upgrade-timeout
            // deaths in the 2026-08-15 soak took 9.7 and 9.9 s) can therefore
            // have its request land AFTER open_socket() cleared it, on a socket
            // that came up a moment ago. On the old code that flag was latent and
            // was spent at the next death; here it would have killed a healthy
            // connection inside one read timeout.
            //
            // So the RX task checks the thing itself rather than trusting what
            // the flag implies — the same rule ARCHITECTURE §9 has paid for
            // twice: supervise on observed state, not on reported events. The
            // predicate is `socket_is_silent()` from ws_supervisor.hpp, the same
            // function the policy decides with, because two call sites comparing
            // against one constant is how the two ends of this drift apart.
            //
            // The flag is deliberately NOT consumed here. die() drops the socket,
            // the next pass of the loop finds `tls_ == nullptr`, and the exchange
            // at the top spends the request on the replacement — one teardown and
            // one attempt, charged to the attempt the supervisor already stamped,
            // with no second 7 s retry cycle of grey in between.
            if (connect_requested_.load(std::memory_order_relaxed) &&
                socket_is_silent(at_us, last_rx_us_.load(std::memory_order_relaxed))) {
                die("rx-silence", 0, 0);
                continue;
            }
            if (!maybe_ping(at_us)) { continue; }
        } else {
            // Unless the floor moved: the association can fall inside the
            // blocking read, in which case lwIP aborts the pcb and the error
            // belongs to the radio rather than to the flow. It is still
            // autopsied — the label `assoc=0` on the line is what separates
            // them — because a death that is not counted is a death that cannot
            // be compared against the old client's.
            die(rc == 0 ? "clean-close" : "read", rc, read_errno);
        }
    }
}

bool WsTransport::open_socket() noexcept {
    // Everything the last socket left behind, cleared before the next one can
    // put a byte anywhere. The reassembler's reset is what makes a half-written
    // message die with its connection instead of being completed by the next.
    parser_.reset();
    reassembler_.reset();
    close_code_ = -1;
    close_reason_[0] = '\0';
    socket_bytes_ = 0;
    fd_ = -1;

    const std::int64_t dns_us = warm_dns();
    const std::int64_t t0 = esp_timer_get_time();
    opened_us_ = t0;
    last_ping_us_ = t0;
    // The silence clock belongs to THIS socket from the moment it is attempted,
    // so a five-minute-old stamp from the connection before it cannot recycle the
    // replacement the instant it comes up. WsSupervisor guards the same case from
    // its own side (it measures from the connect edge it observes); both, because
    // the supervisor's correctness must not depend on this line and this line
    // makes the instrument mean what its name says.
    last_rx_us_.store(t0, std::memory_order_relaxed);

    esp_tls_cfg_t cfg = {};
    // The production trust anchor, unchanged and still pinned: ISRG Root X1/X2
    // in anvil_root_ca.hpp, with the full chain and the reasoning there. The
    // certificate bundle remains unreachable in this framework — that was never
    // the websocket client's doing, so removing it changes nothing here.
    cfg.cacert_buf = reinterpret_cast<const unsigned char*>(kAnvilRootCaPem);
    cfg.cacert_bytes = sizeof(kAnvilRootCaPem);   // PEM: the NUL is part of it
    cfg.timeout_ms = static_cast<int>(kConnectTimeoutMs);

    tls_ = esp_tls_init();
    if (tls_ == nullptr) {
        autopsy("tls-init", -1, ENOMEM);
        return false;
    }
    if (esp_tls_conn_new_sync(kAnvilHost, static_cast<int>(std::strlen(kAnvilHost)), kAnvilPort,
                              &cfg, tls_) != 1) {
        die("connect", -1, errno);
        return false;
    }
    (void)esp_tls_get_conn_sockfd(tls_, &fd_);

    // A read that blocks forever cannot notice anything; a write that blocks
    // forever holds the feed core. Both are bounded here rather than by hope.
    timeval rcv{};
    rcv.tv_sec = kReadTimeoutMs / 1000;
    rcv.tv_usec = (kReadTimeoutMs % 1000) * 1000;
    (void)setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
    timeval snd{};
    snd.tv_sec = 5;
    (void)setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof(snd));

    if (!http_upgrade()) {
        close_socket();
        return false;
    }

    socket_up_.store(true, std::memory_order_relaxed);
    // A request that arrived WHILE this connect was in flight is spent. The
    // supervisor's budget is 7 s and a cold connect has measured 4.2 s, so a
    // slow one can be asked for a second socket before the first has answered;
    // without this the stale flag would survive to the next death and reconnect
    // with no backoff and no association gate.
    connect_requested_.store(false, std::memory_order_relaxed);
    (void)pipe_.post_status(FeedMessage::Kind::Connected);
    ESP_LOGI(kTag, "socket up: dns %d ms, connect+upgrade %d ms, fd %d, rssi %d dBm",
             static_cast<int>(dns_us / 1000),
             static_cast<int>((esp_timer_get_time() - t0) / 1000), fd_,
             static_cast<int>(WiFi.RSSI()));
    return true;
}

bool WsTransport::http_upgrade() noexcept {
    char req[320];
    const int n = std::snprintf(req, sizeof(req),
                                "GET %s HTTP/1.1\r\n"
                                "Host: %s\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Key: %s\r\n"
                                "Sec-WebSocket-Version: 13\r\n"
                                "\r\n",
                                kAnvilPath, kAnvilHost, kWsKey);
    // No Origin header. ARCHITECTURE §7 closed this: M0 measured the deployed
    // upgrade accepting a client that sends none. If that ever changes the
    // server refuses the upgrade loudly, right here, and the one-line fallback
    // is an `Origin: https://anvil.garethcooke.com\r\n` field above.
    if (n <= 0 || n >= static_cast<int>(sizeof(req))) {
        ESP_LOGE(kTag, "upgrade request would not fit — check kAnvilPath");
        return false;
    }
    if (!write_all(req, static_cast<std::size_t>(n))) {
        autopsy("upgrade-write", -1, errno);
        return false;
    }

    // ONE BYTE AT A TIME, and it is not laziness. The header block must be
    // consumed to exactly the blank line and not one byte further, because
    // anything the server sent after it is already a WebSocket frame — reading
    // ahead into a buffer and then discarding the remainder is how a client
    // manufactures the stray leading byte this whole rebuild exists to remove.
    // It costs ~150 single-byte TLS reads, once per connection.
    char hdr[512];
    std::size_t at = 0;
    const std::int64_t deadline = esp_timer_get_time() + kUpgradeBudgetUs;
    bool complete = false;
    while (at + 1 < sizeof(hdr)) {
        const int r = static_cast<int>(esp_tls_conn_read(tls_, hdr + at, 1));
        if (r == 1) {
            ++at;
            if (at >= 4 && std::memcmp(hdr + at - 4, "\r\n\r\n", 4) == 0) {
                complete = true;
                break;
            }
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (esp_timer_get_time() > deadline) {
                autopsy("upgrade-timeout", 0, errno);
                return false;
            }
            continue;
        }
        autopsy(r == 0 ? "upgrade-closed" : "upgrade-read", r, errno);
        return false;
    }
    hdr[at] = '\0';
    if (!complete) {
        ESP_LOGE(kTag, "upgrade headers did not end in %u bytes", static_cast<unsigned>(sizeof(hdr)));
        return false;
    }

    if (std::strstr(hdr, " 101 ") == nullptr) {
        // 120 characters is the status line and the first field or two, which is
        // where a refusal says why.
        ESP_LOGE(kTag, "upgrade refused: %.120s", hdr);
        return false;
    }
    // The check the prototype skipped. It cannot fail on a healthy Anvil, which
    // is the point: if it ever does, something answered the upgrade without
    // doing the SHA1, and finding that out here costs one string compare instead
    // of a socket's worth of frame-parser confusion.
    if (!header_begins_with(hdr, "sec-websocket-accept:", kWsAccept)) {
        ESP_LOGE(kTag, "upgrade accept mismatch — expected %s; headers: %.160s", kWsAccept, hdr);
        return false;
    }
    return true;
}

void WsTransport::close_socket() noexcept {
    // Ours, and it returns at once. This single call is what deleted the spare
    // handle, the 5 s sleeper, the auto-reconnect flag and the retired-handle
    // guard: `esp_websocket_client_stop()` blocked for up to 5 s on a task that
    // could not observe its own stop flag until it woke, and every design
    // decision in ARCHITECTURE §9's 2026-08-10 entry existed to route around it.
    if (tls_ != nullptr) {
        esp_tls_conn_destroy(tls_);
        tls_ = nullptr;
    }
    fd_ = -1;
    // Here and not at the next open: the reassembler is holding a FramePipe slot
    // for whatever half-written message the socket took with it, and a slot that
    // comes back only when the next connection succeeds is a slot lost for the
    // whole of an outage.
    reassembler_.reset();
    parser_.reset();

    // Only a socket that was UP owes the feed task a Disconnected. A connect
    // that never completed leaves the book already stale from the drop that
    // preceded it, and posting a second Gap for it would put a disconnect in the
    // histogram that no connection ever matched.
    if (socket_up_.exchange(false, std::memory_order_relaxed)) {
        (void)pipe_.post_status(FeedMessage::Kind::Disconnected);
    }
}

bool WsTransport::write_all(const void* data, std::size_t len) noexcept {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    std::size_t sent = 0;
    const std::int64_t deadline = esp_timer_get_time() + kWriteBudgetUs;
    while (sent < len) {
        const int rc = static_cast<int>(esp_tls_conn_write(tls_, p + sent, len - sent));
        if (rc > 0) {
            sent += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (esp_timer_get_time() > deadline) { return false; }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        return false;
    }
    return true;
}

bool WsTransport::maybe_ping(std::int64_t now) noexcept {
    if (kClientPingMs == 0) { return true; }
    if (now - last_ping_us_ < static_cast<std::int64_t>(kClientPingMs) * 1000) { return true; }
    last_ping_us_ = now;
    // FIN + ping, masked with an all-zero key: a client MUST mask (§5.3) and an
    // all-zero key is a legal one that leaves the (empty) payload unchanged.
    const std::uint8_t ping[6] = {0x89, 0x80, 0, 0, 0, 0};
    if (!write_all(ping, sizeof(ping))) {
        die("ping-write", -1, errno);
        return false;
    }
    return true;
}

// Autopsy FIRST, teardown second, and the order is the whole reason this
// function exists: autopsy() reads fd_, tls_->error_handle, the parser's frame
// counts and close_code_, and close_socket() destroys or zeroes every one of
// them. Five call sites each kept that ordering by convention; a sixth that
// flipped it would compile and print an autopsy of zeros — a silently degraded
// instrument, which is the one failure this file is not allowed to have.
void WsTransport::die(const char* what, int rc, int saved_errno) noexcept {
    autopsy(what, rc, saved_errno);
    close_socket();
}

void WsTransport::on_ping(const std::uint8_t* payload, std::uint32_t len) noexcept {
    pipe_.count_control();
    // The pong is written from inside the parser's callback, which is safe for
    // one reason worth stating: this is the RX task, and the read that produced
    // these bytes has already returned — there is no mbedtls call on the stack
    // beneath us. Doing it here rather than after feed() also gets the case of
    // two pings in one read right, which a single "pong owed" flag would not.
    std::uint8_t pong[2 + 4 + kMaxControlPayload];
    pong[0] = 0x8A;                                              // FIN + pong
    pong[1] = static_cast<std::uint8_t>(0x80u | (len & 0x7Fu));   // masked, len <= 125
    std::memset(pong + 2, 0, 4);
    if (len > 0) { std::memcpy(pong + 6, payload, len); }
    if (!write_all(pong, 6u + len)) {
        // A short or failed pong desyncs nothing here (we send whole frames or
        // fail), but a server that enforces pongs will close, and this line is
        // what stops that reading as a server-side kill. Logged on a live socket
        // — the one place this firmware does — because it is rare, it means the
        // socket is already dying, and without it the next autopsy has no cause.
        ESP_LOGW(kTag, "pong write failed (errno %d) — the socket is going", errno);
    }
}

void WsTransport::on_close(std::uint16_t code, const char* reason,
                           std::uint32_t reason_len) noexcept {
    // NOT counted as a control frame, and the asymmetry with on_ping/on_pong is
    // deliberate. FrameReassembler has always treated a close as a no-op rather
    // than a control count ("a close frame is ignored here; the disconnect path
    // does the work"), so counting it here would make `control` mean a different
    // thing in the two build arms — and the only reason the espws arm still
    // exists is to have its counters compared against this one.
    close_code_ = static_cast<int>(code);
    const std::size_t room = sizeof(close_reason_) - 1;
    const std::size_t n = (reason_len < room) ? reason_len : room;
    if (n > 0) { std::memcpy(close_reason_, reason, n); }
    close_reason_[n] = '\0';
    // The socket is not closed here — rx_main() does it, after feed() returns
    // and the buffer it is walking is no longer in use.
}

void WsTransport::autopsy(const char* what, int rc, int saved_errno) noexcept {
    const std::int64_t now = esp_timer_get_time();
    const std::int64_t lifetime_ms = (opened_us_ > 0) ? (now - opened_us_) / 1000 : 0;

    int so_error = 0;
    socklen_t so_len = sizeof(so_error);
    if (fd_ >= 0) { (void)getsockopt(fd_, SOL_SOCKET, SO_ERROR, &so_error, &so_len); }

    int tls_code = 0;
    int tls_flags = 0;
    if (tls_ != nullptr && tls_->error_handle != nullptr) {
        (void)esp_tls_get_and_clear_last_error(tls_->error_handle, &tls_code, &tls_flags);
    }

    char mbed[96] = "n/a";
    if (rc < 0) { mbedtls_strerror(rc, mbed, sizeof(mbed)); }

    // `deaths_` counts SOCKET ENDS, and since 2026-08-16 that includes one this
    // firmware chose: `[rx-silence]`, the supervisor's five-minute recycle. The
    // ordinal series is deliberately shared — every socket end deserves an
    // autopsy and an ordinal — but the acceptance bar this milestone cleared is
    // phrased as "zero errno-silent deaths", so a future soak's `socket end #N`
    // count no longer means quite what it did. Read the label, not the number:
    // `[read]` and `[clean-close]` are the wire's; `[rx-silence]` is ours.
    ++deaths_;
    ESP_LOGW(kTag, "socket end #%u [%s]: %d ms, %llu bytes, %u data / %u ctrl frames",
             static_cast<unsigned>(deaths_), what, static_cast<int>(lifetime_ms),
             static_cast<unsigned long long>(socket_bytes_),
             static_cast<unsigned>(parser_.data_frames()),
             static_cast<unsigned>(parser_.control_frames()));
    ESP_LOGW(kTag, "  rc=%s0x%04X (%s) errno=%d (%s) so_error=%d esp_tls=0x%X/0x%X",
             (rc < 0) ? "-" : "", static_cast<unsigned>((rc < 0) ? -rc : rc), mbed, saved_errno,
             std::strerror(saved_errno), so_error, static_cast<unsigned>(tls_code),
             static_cast<unsigned>(tls_flags));
    // BYTES, not words: ESP-IDF's port defines StackType_t as uint8_t, so the
    // high-water mark comes back in bytes (feed_task.hpp quotes the same trap
    // for stack SIZES). The first version printed "words", which overstated the
    // margin 4x on the line whose stated job is to lower kRxTaskStack safely.
    ESP_LOGW(kTag, "  rssi=%d dBm assoc=%d stack_free=%u B",
             static_cast<int>(WiFi.RSSI()), (WiFi.status() == WL_CONNECTED) ? 1 : 0,
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    if (close_code_ >= 0) {
        ESP_LOGW(kTag, "  ws close frame: code %d reason '%s' <- the server SAID why", close_code_,
                 close_reason_);
    }
}

}  // namespace depthcharge::fw
