// Push-to-talk flow: hold power, start listening as early as possible, stream the turn to the
// home server over Wi-Fi while the button is held, play the server's reply, power off.
// Protocol: docs/protocol.md. The server is an echo for now (server/mic_server.py).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "amp.h"
#include "battery.h"
#include "board.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "mic.h"
#include "power.h"
#include "session.h"
#include "wifi.h"

namespace {

const char *TAG = "main";

// Skeleton behavior: always record at least this long, so a tap (or a reset over USB) still
// records something, and the summary prints after USB has re-enumerated (~1.5 s).
constexpr int MIN_RECORD_MS = 2000;
// The button must read released this long before recording stops (contact bounce).
constexpr int RELEASE_MS = 50;
// Keep recording this long after the release: people let go while finishing the last word
// (bench, 2026-10-24: full speech level in the last 100 ms, and Gemini's transcript lost the word).
constexpr int RELEASE_TAIL_MS = 250;
// Reply audio to buffer before playback starts, so network jitter doesn't starve the speaker.
constexpr int PREBUFFER_MS = 150;
// Give up on a reply that stops arriving mid-way for this long.
constexpr int REPLY_STALL_MS = 5000;
// Every recording starts with a ~100 ms mic start-up burst; it's never sent or measured.
constexpr size_t SETTLE_SAMPLES = mic::SAMPLE_RATE / 10;
// Speech peaks ~1400 of 32767 at arm's length; 16x brings them to ~70% of full scale.
// Applied to the uplink (docs/protocol.md); automatic gain control later.
constexpr float UPLINK_GAIN = 16;
constexpr size_t UPLINK_CHUNK = mic::SAMPLE_RATE / 10;  // 100 ms per frame; the backlog goes out fast

constexpr int WIFI_TIMEOUT_MS = 10000;  // from boot; a cold association normally takes 1-3 s
constexpr int WS_TIMEOUT_MS = 3000;
constexpr int REPLY_TIMEOUT_MS = 15000;  // after release; the AI may call tools first

// Shared between app_main and the uplink task for one run.
std::atomic<bool> released{false};
std::atomic<int> uplink_result{0};  // 0 running, 1 turn sent, -1 failed
int64_t ws_open_us = 0;
int battery_mv = 0;
char device_id[16];

void delay_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// Mean (DC offset), and peak/RMS around the mean, of samples [from, to).
void log_span(const char *label, size_t from, size_t to)
{
    const int16_t *s = mic::samples();
    to = std::min(to, mic::sample_count());
    if (from >= to) {
        return;
    }
    int64_t sum = 0;
    for (size_t i = from; i < to; i++) {
        sum += s[i];
    }
    int mean = int(sum / int64_t(to - from));
    int peak = 0;
    int64_t sum_sq = 0;
    for (size_t i = from; i < to; i++) {
        int v = s[i] - mean;
        peak = std::max(peak, std::abs(v));
        sum_sq += int64_t(v) * v;
    }
    int rms = int(std::sqrt(double(sum_sq) / double(to - from)));
    ESP_LOGI(TAG, "%-8s %5d-%5d ms: mean %6d, peak %5d, rms %5d (around the mean)", label,
             int(from * 1000 / mic::SAMPLE_RATE), int(to * 1000 / mic::SAMPLE_RATE), mean, peak, rms);
}

// Connects, then streams the recording from the PSRAM buffer as it grows: first the backlog
// captured while Wi-Fi came up, then live audio, until the button is released.
void uplink_task(void *)
{
    bool ok = wifi::wait_connected(WIFI_TIMEOUT_MS) && session::open(WS_TIMEOUT_MS);
    if (ok) {
        ws_open_us = esp_timer_get_time();
        char msg[160];
        snprintf(msg, sizeof(msg),
                 R"({"type":"hello","v":1,"device_id":"%s","firmware":"dev","battery_mv":%d})", device_id,
                 battery_mv);
        ok = session::send_text(msg) && session::send_text(R"({"type":"turn_start","turn_id":1})");
    }

    size_t sent = SETTLE_SAMPLES;
    int16_t chunk[UPLINK_CHUNK];
    while (ok) {
        bool done = released;  // read before the count: after release the count is final
        size_t available = mic::sample_count();
        if (sent < available) {
            size_t n = std::min(UPLINK_CHUNK, available - sent);
            const int16_t *src = mic::samples() + sent;
            for (size_t i = 0; i < n; i++) {
                chunk[i] = int16_t(std::clamp(src[i] * UPLINK_GAIN, -32768.0f, 32767.0f));
            }
            ok = session::send_audio(chunk, n);
            sent += n;
        } else if (done) {
            break;
        } else {
            delay_ms(10);
        }
    }
    if (ok) {
        char msg[96];
        snprintf(msg, sizeof(msg), R"({"type":"turn_end","turn_id":1,"audio_ms":%d})",
                 int((sent - SETTLE_SAMPLES) * 1000 / mic::SAMPLE_RATE));
        ok = session::send_text(msg);
    }
    uplink_result = ok ? 1 : -1;
    vTaskDelete(nullptr);
}

void show_error()
{
    led::set(40, 0, 0);
    delay_ms(1500);
    led::set(0, 0, 0);
}

}  // namespace

extern "C" void app_main()
{
    power::init();
    // esp_timer starts during app start-up, after the ROM and bootloader have run, so its times
    // are NOT since reset or power-on: app_main reads ~2 ms, while film shows ~58 ms from latch on.
    int64_t boot_us = esp_timer_get_time();

    // Boot indicator: blue as soon as our code runs. Filming the latch LED and this one in
    // slow motion measures press-to-code time (2026-10-24: ~58 ms; Arduino was ~130 ms).
    led::init();
    led::set(0, 0, 40);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "mic-%02x%02x%02x", mac[3], mac[4], mac[5]);

    esp_log_level_set("*", ESP_LOG_INFO);
    for (int run = 1;; run++) {
        // Listen before anything slow: the goal is under 300 ms from press to listening, with
        // audio buffered in PSRAM until the network is up. Green = audio arriving.
        bool listening =
            mic::start(xTaskGetCurrentTaskHandle()) && ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) > 0;
        led::set(listening ? 0 : 40, listening ? 40 : 0, 0);  // red if the mic never delivered
        int64_t listen_us = esp_timer_get_time();

        // Then the network, in the background. Wi-Fi stays up across runs on USB.
        battery_mv = battery::read_mv();
        wifi::start();
        released = false;
        uplink_result = 0;
        ws_open_us = 0;
        if (listening) {
            xTaskCreate(uplink_task, "uplink", 8192, nullptr, 5, nullptr);
        }

        // Push-to-talk: record while S1 is held. On battery it's still held from the press that
        // powered us on.
        int released_ms = 0;
        while (listening && released_ms < RELEASE_MS && mic::sample_count() < mic::capacity()) {
            delay_ms(10);
            bool min_done = esp_timer_get_time() - listen_us >= MIN_RECORD_MS * 1000LL;
            released_ms = (!power::ptt_pressed() && min_done) ? released_ms + 10 : 0;
        }
        int64_t release_us = esp_timer_get_time();
        if (listening) {
            delay_ms(RELEASE_TAIL_MS);
        }
        mic::stop();
        released = true;
        led::set(30, 20, 0);  // amber: waiting for the reply

        // Wait for the uplink to finish sending, then for the reply.
        while (listening && uplink_result == 0) {
            delay_ms(10);
        }
        // Play the reply while it streams in: start once PREBUFFER_MS has arrived (or all of it),
        // then keep feeding the amp; amp::write blocks, which paces this loop to real time.
        bool replied = uplink_result == 1 && session::wait_reply_start(REPLY_TIMEOUT_MS);
        int64_t play_us = 0;
        int64_t reply_us = 0;
        size_t played = 0;
        if (replied) {
            const int rate = std::max(1, session::reply_sample_rate());
            const size_t prebuffer = size_t(rate) * PREBUFFER_MS / 1000;
            const size_t block = size_t(rate) / 50;  // 20 ms per write
            while (!session::reply_complete() && !session::failed() && session::reply_sample_count() < prebuffer) {
                delay_ms(5);
            }
            bool speaking = amp::begin(rate);
            led::set(20, 0, 20);  // purple: playing the reply
            play_us = esp_timer_get_time();
            int64_t last_audio_us = play_us;
            while (true) {
                bool done = session::reply_complete();  // read before the count: then the count is final
                size_t available = session::reply_sample_count();
                if (done && !reply_us) {
                    reply_us = esp_timer_get_time();
                }
                if (played < available) {
                    size_t n = std::min(block, available - played);
                    if (speaking) {
                        amp::write(session::reply_samples() + played, n, 1.0f, done && played + n == available);
                    }
                    played += n;
                    last_audio_us = esp_timer_get_time();
                } else if (done || session::failed() ||
                           esp_timer_get_time() - last_audio_us > REPLY_STALL_MS * 1000LL) {
                    break;  // finished, or the link dropped / went quiet mid-reply
                } else {
                    delay_ms(5);  // reply running behind playback
                }
            }
            if (speaking) {
                amp::end();
                char msg[112];
                snprintf(msg, sizeof(msg), R"({"type":"playback_stopped","reply_id":%d,"played_ms":%d,"reason":"done"})",
                         session::reply_id(), int(played * 1000 / rate));
                session::send_text(msg);
            }
            led::set(0, 0, 0);
        } else {
            show_error();
        }
        session::close();

        // Logged at the end because USB drops everything from the first ~1.5 s after a reset.
        auto ms = [](int64_t us) { return us ? int(us / 1000) : -1; };
        ESP_LOGI(TAG, "run %d  %s  since app start: app_main %d, listening %d, Wi-Fi %d, WebSocket %d ms", run,
                 device_id, ms(boot_us), ms(listen_us), ms(wifi::connected_us()), ms(ws_open_us));
        wifi::Timing w = wifi::timing();
        session::Timing s = session::timing();
        ESP_LOGI(TAG, "  net (ms since app start): wifi start %d, driver up %d, associated %d (%s, %d kicks), got IP %d | "
                      "ws open %d, ARP done %d (%d missed), connected %d",
                 ms(w.start), ms(w.driver_up), ms(w.associated),
                 wifi::used_cached_ap() ? "cached AP" : "scan", w.kicks, ms(w.got_ip), ms(s.open), ms(s.arp_done),
                 s.arp_misses, ms(s.connected));
        log_span("uplink", SETTLE_SAMPLES, mic::sample_count());
        if (replied) {
            ESP_LOGI(TAG, "reply %d: %d ms at %d Hz; release -> first reply audio %d, playing %d, all arrived %d ms",
                     session::reply_id(), int(played * 1000 / std::max(1, session::reply_sample_rate())),
                     session::reply_sample_rate(), ms(session::reply_first_audio_us() - release_us),
                     ms(play_us - release_us), ms(reply_us - release_us));
        } else {
            ESP_LOGE(TAG, "no reply (%s)", uplink_result == 1 ? "server didn't answer" : "couldn't reach the server");
        }
        ESP_LOGI(TAG, "battery %d mV", battery_mv);

        wifi::stop();
        power::off();

        // Only reached on USB, where there's no power to cut. Act as if powered off, and treat
        // the next S1 press like the one that would power us on, so USB testing matches battery
        // use. (Holding S1 while pressing reset proved too awkward to time.)
        ESP_LOGI(TAG, "on USB: press S1 to record again");
        while (!power::ptt_pressed()) {
            delay_ms(10);
        }
        led::set(0, 0, 40);
    }
}
