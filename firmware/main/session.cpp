#include "session.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "credentials.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi.h"

namespace session {
namespace {

const char *TAG = "session";

constexpr EventBits_t CONNECTED = BIT0;
constexpr EventBits_t REPLY_DONE = BIT1;
constexpr EventBits_t FAILED = BIT2;
constexpr EventBits_t REPLY_STARTED = BIT3;

// Replies can come at up to 24 kHz (the realtime AIs' output rate). 30 s fits easily in PSRAM.
constexpr size_t REPLY_CAPACITY_BYTES = 24000 * 30 * sizeof(int16_t);
// Bigger than the server's 3200-byte audio frames, so each arrives in one event.
constexpr int WS_BUFFER = 4096;

esp_websocket_client_handle_t client = nullptr;
EventGroupHandle_t events = nullptr;

// Written by the WebSocket task; read by the player while the reply streams in.
uint8_t *reply = nullptr;
std::atomic<size_t> reply_bytes{0};
std::atomic<int> reply_rate{0};
std::atomic<int> current_reply{0};
std::atomic<bool> in_reply{false};
std::atomic<int64_t> first_audio_us{0};

Timing times{};

char text[1024];  // one JSON message, reassembled if it arrives in pieces
size_t text_len = 0;

int int_field(const cJSON *msg, const char *name)
{
    const cJSON *v = cJSON_GetObjectItem(msg, name);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

void on_message(const char *json)
{
    cJSON *msg = cJSON_Parse(json);
    const char *type = msg ? cJSON_GetStringValue(cJSON_GetObjectItem(msg, "type")) : nullptr;
    if (!type) {
        ESP_LOGW(TAG, "unreadable message: %.60s", json);
    } else if (!strcmp(type, "reply_start")) {
        current_reply = int_field(msg, "reply_id");
        reply_rate = int_field(msg, "sample_rate");
        reply_bytes = 0;
        first_audio_us = 0;
        in_reply = true;
        xEventGroupSetBits(events, REPLY_STARTED);
    } else if (!strcmp(type, "reply_end")) {
        in_reply = false;
        xEventGroupSetBits(events, REPLY_DONE);
    } else if (!strcmp(type, "error")) {
        const char *m = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "message"));
        ESP_LOGE(TAG, "server error: %s", m ? m : "?");
        xEventGroupSetBits(events, FAILED);
    } else {
        ESP_LOGW(TAG, "unknown message type %s", type);
    }
    cJSON_Delete(msg);
}

void on_data(const esp_websocket_event_data_t *d)
{
    if (d->op_code == 0x2) {  // binary: reply audio
        if (!in_reply) {
            return;  // audio for a reply we're not playing (e.g. after an interrupt): drop
        }
        if (first_audio_us == 0) {
            first_audio_us = esp_timer_get_time();
        }
        size_t at = reply_bytes;
        size_t n = std::min<size_t>(d->data_len, REPLY_CAPACITY_BYTES - at);
        memcpy(reply + at, d->data_ptr, n);
        reply_bytes = at + n;  // publish only after the copy, for the player
    } else if (d->op_code == 0x1) {  // text: JSON control message, possibly in pieces
        if (d->payload_offset == 0) {
            text_len = 0;
        }
        size_t n = std::min<size_t>(d->data_len, sizeof(text) - 1 - text_len);
        memcpy(text + text_len, d->data_ptr, n);
        text_len += n;
        if (d->payload_offset + d->data_len >= d->payload_len) {
            text[text_len] = '\0';
            on_message(text);
        }
    }
    // Other opcodes (ping, pong, close) are handled by the client itself.
}

void on_event(void *, esp_event_base_t, int32_t id, void *data)
{
    switch (id) {
    case WEBSOCKET_EVENT_BEFORE_CONNECT:
        times.before_connect = esp_timer_get_time();
        break;
    case WEBSOCKET_EVENT_CONNECTED:
        times.connected = esp_timer_get_time();
        xEventGroupSetBits(events, CONNECTED);
        break;
    case WEBSOCKET_EVENT_DATA:
        on_data(static_cast<esp_websocket_event_data_t *>(data));
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        xEventGroupClearBits(events, CONNECTED);
        xEventGroupSetBits(events, FAILED);
        break;
    default:
        break;
    }
}

}  // namespace

bool open(int timeout_ms)
{
    if (!events) {
        events = xEventGroupCreate();
    }
    if (!reply) {
        reply = static_cast<uint8_t *>(heap_caps_malloc(REPLY_CAPACITY_BYTES, MALLOC_CAP_SPIRAM));
        if (!reply) {
            ESP_LOGE(TAG, "no PSRAM for the reply buffer");
            return false;
        }
    }
    times = {};
    times.open = esp_timer_get_time();
    times.arp_misses = wifi::warm_arp(secrets::SERVER_HOST, 1000);
    times.arp_done = esp_timer_get_time();
    xEventGroupClearBits(events, CONNECTED | REPLY_DONE | FAILED | REPLY_STARTED);
    reply_bytes = 0;
    reply_rate = 0;
    in_reply = false;
    first_audio_us = 0;

    static char uri[96];
    static char headers[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/device", secrets::SERVER_HOST, secrets::SERVER_PORT);
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", secrets::DEVICE_TOKEN);

    esp_websocket_client_config_t cfg = {};
    cfg.uri = uri;
    cfg.headers = headers;
    cfg.buffer_size = WS_BUFFER;
    cfg.network_timeout_ms = timeout_ms;
    cfg.disable_auto_reconnect = true;  // one connection per press; a drop ends the turn (v1)
    client = esp_websocket_client_init(&cfg);
    if (!client) {
        return false;
    }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_event, nullptr);
    if (esp_websocket_client_start(client) != ESP_OK) {
        close();
        return false;
    }
    EventBits_t bits =
        xEventGroupWaitBits(events, CONNECTED | FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (!(bits & CONNECTED)) {
        ESP_LOGE(TAG, "can't reach %s", uri);
        close();
        return false;
    }
    return true;
}

bool send_text(const char *json)
{
    return client && esp_websocket_client_send_text(client, json, strlen(json), pdMS_TO_TICKS(2000)) >= 0;
}

bool send_audio(const int16_t *samples, size_t n)
{
    return client && esp_websocket_client_send_bin(client, reinterpret_cast<const char *>(samples),
                                                   int(n * sizeof(int16_t)), pdMS_TO_TICKS(2000)) >= 0;
}

bool wait_reply_start(int timeout_ms)
{
    EventBits_t bits =
        xEventGroupWaitBits(events, REPLY_STARTED | FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return bits & REPLY_STARTED;
}

bool reply_complete()
{
    return xEventGroupGetBits(events) & REPLY_DONE;
}

bool failed()
{
    return xEventGroupGetBits(events) & FAILED;
}

const int16_t *reply_samples()
{
    return reinterpret_cast<const int16_t *>(reply);
}

size_t reply_sample_count()
{
    return reply_bytes / sizeof(int16_t);
}

int reply_sample_rate()
{
    return reply_rate;
}

int reply_id()
{
    return current_reply;
}

int64_t reply_first_audio_us()
{
    return first_audio_us;
}

Timing timing()
{
    return times;
}

void close()
{
    if (client) {
        esp_websocket_client_close(client, pdMS_TO_TICKS(500));
        esp_websocket_client_destroy(client);
        client = nullptr;
    }
}

}  // namespace session
