// firmware/src/ws_transport.cpp — see ws_transport.hpp.
#include "ws_transport.hpp"

#include <WiFi.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "anvil_root_ca.hpp"

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "ws";
}  // namespace

bool WsTransport::connect_wifi(const char* ssid, const char* password,
                               std::uint32_t timeout_ms) noexcept {
    WiFi.mode(WIFI_STA);
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
    WiFi.begin(ssid, password);

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
    ESP_LOGI(kTag, "wifi up: ip=%s rssi=%d dBm | power-save requested=%s driver ps=%d%s"
                   " (0=NONE 1=MIN_MODEM 2=MAX_MODEM)",
             WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()),
             kWifiPowerSave ? "ON" : "OFF", static_cast<int>(ps),
             (ps_err == ESP_OK) ? "" : " (read failed)");
    return true;
}

bool WsTransport::start() noexcept {
    esp_websocket_client_config_t cfg = {};
    cfg.uri = kAnvilUri;
    cfg.buffer_size = kWsRxBufferBytes;

    // TLS trust anchor: a pinned root, because the certificate bundle is not
    // reachable through this vintage of esp_websocket_client_config_t. The full
    // reasoning, the measured chain and the verification are in anvil_root_ca.hpp.
    cfg.cert_pem = kAnvilRootCaPem;
    cfg.cert_len = 0;  // NUL-terminated PEM

    // No Origin header. ARCHITECTURE §7 closed this: M0 measured the deployed
    // upgrade accepting a client that sends none. If that ever changes, the
    // server rejects the upgrade loudly at connect and the one-line fallback is:
    //     cfg.headers = "Origin: https://anvil.garethcooke.com\r\n";

    // Auto-reconnect stays enabled (disable_auto_reconnect defaults false), and
    // the pingpong watchdog stays on: if the socket dies quietly the client will
    // tear it down and tell us, which arrives here as DISCONNECTED and becomes
    // Gap{Disconnect}. Our own RX watchdog is the faster and more honest of the
    // two — it fires on the *data* stopping, not on the socket dying — but both
    // paths exist because a half-open TCP connection can outlive a feed.
    //
    // What is NOT set here, and was meant to be: the interval that auto-reconnect
    // waits between attempts. There is no `reconnect_timeout_ms` in this
    // vintage's config struct — the field arrives in a later IDF than the one
    // Arduino-ESP32 2.0.14 ships — so the library's 10 s
    // WEBSOCKET_RECONNECT_TIMEOUT_MS stands and is unreachable from here. The
    // 2 s cadence is enforced by supervise() preempting it instead.
    //
    // Left enabled all the same, but on evidence it earns little: across two
    // bench outages it held the connection down for the full 12 s the supervisor
    // gave it and recovered neither. Whether it is retrying and failing or not
    // retrying at all is not visible from here, because everything it would say
    // about it is an ESP_LOGI compiled out of the shipped archive.
    cfg.ping_interval_sec = 10;
    cfg.pingpong_timeout_sec = 20;

    // The client creates its own task, which this IDF vintage gives no way to
    // pin (there is no task_core_id field). That is acceptable precisely because
    // this task does nothing but memcpy into a slot and post it: the engine work
    // is all on the Core 0 feed task. Give it enough stack for mbed-TLS.
    cfg.task_stack = 6144;
    cfg.task_prio = 5;

    client_ = esp_websocket_client_init(&cfg);
    if (client_ == nullptr) {
        ESP_LOGE(kTag, "esp_websocket_client_init failed");
        return false;
    }

    if (esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY,
                                      &WsTransport::event_trampoline, this) != ESP_OK) {
        ESP_LOGE(kTag, "esp_websocket_register_events failed");
        return false;
    }

    ESP_LOGI(kTag, "connecting to %s", kAnvilUri);
    const esp_err_t err = esp_websocket_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_websocket_client_start: %s", esp_err_to_name(err));
        return false;
    }

    // The boot connection is attempt #1, and marking it is not bookkeeping — it
    // is what buys it the same handshake immunity every retry gets. Without this
    // supervise() sees a client that has simply never been connected, cannot
    // tell that from one that dropped, and restarts it 2 s in: straight through
    // the cold TLS handshake, which the 2026-08-09 18:15 log caught it doing
    // (`attempt #2` for the first outage, and connects=2 before any outage had
    // happened). It recovered, so it read as noise rather than as a bug.
    note_attempt_begun();
    return true;
}

void WsTransport::note_attempt_begun() noexcept {
    ++attempts_;
    attempt_started_us_ = esp_timer_get_time();
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
    if (client_ == nullptr) { return; }

    const std::int64_t now = esp_timer_get_time();
    // Sampled on every pass, connected or not, and before the early return
    // below — the rssi through a hole is exactly the number a link verdict has
    // to be checked against, so it must not stop being collected precisely when
    // the transport is unhappy.
    sample_rssi(now);

    if (esp_websocket_client_is_connected(client_)) {
        if (attempt_started_us_ != 0) {
            // The measurement kHandshakeBudgetUs is guessed from, printed on
            // every recovery so the constant stops being a guess. This is the
            // SOCKET half only; the panel prints its own "grey for N ms", and
            // the difference between the two numbers is how long Anvil took to
            // send the snapshot that grants LIVE. One 6.1 s figure covering both
            // was all the last bench run could offer.
            ESP_LOGI(kTag, "socket up %d ms into attempt #%u",
                     static_cast<int>((now - attempt_started_us_) / 1000),
                     static_cast<unsigned>(attempts_));
            attempt_started_us_ = 0;
        }
        disconnected_since_us_ = 0;
        return;
    }

    if (disconnected_since_us_ == 0) {
        disconnected_since_us_ = now;
        return;
    }

    // The first attempt of an outage falls a backoff after the feed died; every
    // later one falls a full cycle after the previous attempt STARTED. Because
    // the cycle is the backoff plus the handshake budget, this one comparison is
    // also what guarantees an attempt of ours is never disturbed inside its
    // budget — restarting a client mid-handshake is the single way this function
    // could turn a recoverable outage into a permanent one, and it is ruled out
    // on a clock we own rather than on anything the library tells us.
    const std::int64_t due = (attempt_started_us_ == 0)
                                 ? disconnected_since_us_ + kReconnectBackoffUs
                                 : attempt_started_us_ + kRetryCycleUs;
    if (now < due) { return; }

    // Stop then start: stop() is a no-op on an already-dead task and start()
    // recreates it. Safe here because this is a normal task context, never the
    // event callback. This covers every way the feed can be down with the one
    // action, because the bench showed there is no case where waiting longer
    // helped — the client's own auto-reconnect had 12 s, twice, and used it to
    // do nothing.
    ESP_LOGW(kTag, "websocket down %d s — restarting client (attempt #%u)",
             static_cast<int>((now - disconnected_since_us_) / 1000000),
             static_cast<unsigned>(attempts_ + 1));
    (void)esp_websocket_client_stop(client_);
    const esp_err_t err = esp_websocket_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "restart failed: %s", esp_err_to_name(err));
    }
    // Marked AFTER the calls, not before: stop() blocks until the client's task
    // has joined — 2545 ms of it on the 18:22 bench run — and letting a slow
    // teardown eat the handshake budget would put the next restart back on top
    // of this attempt.
    note_attempt_begun();
}

void WsTransport::event_trampoline(void* arg, esp_event_base_t /*base*/, std::int32_t id,
                                   void* event_data) noexcept {
    static_cast<WsTransport*>(arg)->on_event(
        id, static_cast<esp_websocket_event_data_t*>(event_data));
}

void WsTransport::on_event(std::int32_t id, esp_websocket_event_data_t* data) noexcept {
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            (void)pipe_.post_status(FeedMessage::Kind::Connected);
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
        case WEBSOCKET_EVENT_ERROR:
            // These three are reported to the feed task and drive NOTHING else,
            // and that is a correction rather than an omission.
            //
            // The retry used to be armed here, on ERROR and CLOSED, on the
            // reasoning that only a definitively-ended attempt may schedule the
            // next one — sound in itself, and it never fired. Two bench outages
            // on 2026-08-09 recovered on the 12 s backstop instead, which is
            // proof the flag was never set, because the armed path returns early
            // and the backstop could not have run if it had been. What this
            // vintage actually dispatches for a dead socket is DISCONNECTED, from
            // abort_connection(); the `Error receive data` line in the log is the
            // library's own ESP_LOGE on the way there, not an ERROR event.
            //
            // Arming on DISCONNECTED instead was the obvious repair and is the
            // wrong one: abort_connection() is also what our own stop() runs
            // through, so each restart would re-arm the timer that caused it.
            // The supervisor polls the socket state directly now, which needs no
            // theory about which of these three the library will choose.
            //
            // Whatever half-written message we held is gone with the socket;
            // drop it before telling the feed task, so the next connection
            // starts from a clean slot.
            reassembler_.reset();
            (void)pipe_.post_status(FeedMessage::Kind::Disconnected);
            // Here to settle which event id this library really raises, since
            // the answer cost an outage to find out and is not written down
            // anywhere in the shipped headers.
            //
            // It is NOT a free line, and the bench output says so: it prints as
            // `[101102][W][ws_transport.cpp:224] on_event(): ...`, Arduino's
            // log_w format, not ESP-IDF's `W (101102) ws:`. That is because this
            // header includes <Arduino.h> first for the INADDR_NONE fix, so
            // esp32-hal-log.h redefines ESP_LOGW to log_w — the log_printfv path
            // that mallocs past 64 chars (this line is ~67) and takes the UART
            // mutex with portMAX_DELAY, which feed_task.cpp had all its logging
            // removed for. Accepted here and not there: this runs on the client
            // task, once, on a socket that has already stopped carrying data, so
            // there is no frame for it to delay. Anything that ever logs from
            // this handler on a LIVE socket has to re-open that argument.
            ESP_LOGW(kTag, "ws down: event %d", static_cast<int>(id));
            break;

        case WEBSOCKET_EVENT_DATA: {
            if (data == nullptr) { break; }
            WsChunk chunk;
            chunk.op_code = data->op_code;
            chunk.payload_offset =
                (data->payload_offset > 0) ? static_cast<std::uint32_t>(data->payload_offset) : 0u;
            chunk.payload_len =
                (data->payload_len > 0) ? static_cast<std::uint32_t>(data->payload_len) : 0u;
            chunk.data = data->data_ptr;
            chunk.data_len = (data->data_len > 0) ? static_cast<std::uint32_t>(data->data_len) : 0u;
            // The arrival stamp, taken as far upstream as this firmware can
            // reach: inside the client's own callback, before anything of ours
            // has had a chance to be late. Everything after this point is on our
            // side of the line, which is what makes the arrival-vs-event split a
            // real split rather than two views of the same delay.
            chunk.arrival_us = esp_timer_get_time();
            reassembler_.on_chunk(chunk);
            break;
        }

        default:
            break;
    }
}

}  // namespace depthcharge::fw
