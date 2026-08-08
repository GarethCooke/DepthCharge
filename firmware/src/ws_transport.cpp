// firmware/src/ws_transport.cpp — see ws_transport.hpp.
#include "ws_transport.hpp"

#include <WiFi.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "anvil_root_ca.hpp"

namespace depthcharge::fw {
namespace {
constexpr const char* kTag = "ws";
}  // namespace

bool WsTransport::connect_wifi(const char* ssid, const char* password,
                               std::uint32_t timeout_ms) noexcept {
    WiFi.mode(WIFI_STA);
    // Modem sleep off. The default power-save mode parks the radio between DTIM
    // beacons and can add hundreds of milliseconds of RX latency — which on a
    // feed whose worst healthy inter-frame gap is 640 ms against a 1000 ms
    // watchdog would manufacture outages that never happened. This is a
    // mains-powered desk object; the power cost is irrelevant and the latency is
    // not.
    WiFi.setSleep(false);
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

    ESP_LOGI(kTag, "wifi up: ip=%s rssi=%d dBm", WiFi.localIP().toString().c_str(),
             static_cast<int>(WiFi.RSSI()));
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
    return true;
}

void WsTransport::supervise() noexcept {
    if (client_ == nullptr) { return; }

    if (esp_websocket_client_is_connected(client_)) {
        disconnected_since_us_ = 0;
        return;
    }

    const std::int64_t now = esp_timer_get_time();
    if (disconnected_since_us_ == 0) {
        disconnected_since_us_ = now;
        return;
    }
    if (now - disconnected_since_us_ < kReviveAfterUs) { return; }

    // Long enough that the client's own auto-reconnect has had its chance and
    // not taken it — which means either a clean close (where it never applies)
    // or a wedged client. Stop then start: stop() is a no-op on an already-dead
    // task and start() recreates it. Safe here because this is a normal task
    // context, never the event callback.
    ++revivals_;
    ESP_LOGW(kTag, "websocket down for %d s and not recovering — restarting client (#%u)",
             static_cast<int>((now - disconnected_since_us_) / 1000000),
             static_cast<unsigned>(revivals_));
    (void)esp_websocket_client_stop(client_);
    const esp_err_t err = esp_websocket_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "restart failed: %s", esp_err_to_name(err));
    }
    // Restart the clock either way, so a persistently failing restart retries on
    // the same cadence instead of hammering the socket.
    disconnected_since_us_ = now;
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
            // Whatever half-written message we held is gone with the socket;
            // drop it before telling the feed task, so the next connection
            // starts from a clean slot.
            reassembler_.reset();
            (void)pipe_.post_status(FeedMessage::Kind::Disconnected);
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
            reassembler_.on_chunk(chunk);
            break;
        }

        default:
            break;
    }
}

}  // namespace depthcharge::fw
