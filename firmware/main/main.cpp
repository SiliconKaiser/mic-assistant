// Push-to-talk flow: hold power, start listening as early as possible, stream the turn to the
// home server over Wi-Fi while the button is held, play the server's reply, power off.
// Pressing S1 again while waiting for or playing a reply interrupts it and starts a new turn on
// the same connection. Protocol: docs/protocol.md. Server: server/mic_server.py.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
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

// Skeleton behavior: the first turn always records at least this long, so a tap (or a reset over
// USB) still records something, and the summary prints after USB has re-enumerated (~1.5 s).
constexpr int MIN_RECORD_MS = 2000;
// The button must read released this long before recording stops (contact bounce).
constexpr int RELEASE_MS = 50;
// Keep recording this long after the release: people let go while finishing the last word
// (bench, 2026-10-24: full speech level in the last 100 ms, and Gemini's transcript lost the word).
constexpr int RELEASE_TAIL_MS = 250;
// A press while waiting for or playing a reply must read this long to count as an interrupt.
// PTT reads S1 through the latch's A pin, which can glitch "pressed" when the supply sags (seen
// with a meter in series on the bench), and loud playback on a weak pack is when that happens.
constexpr int INTERRUPT_PRESS_MS = 30;
// Reply audio to buffer before playback starts, so network jitter doesn't starve the speaker.
constexpr int PREBUFFER_MS = 150;
// Give up on a reply that stops arriving mid-way for this long.
constexpr int REPLY_STALL_MS = 5000;
// Audio queued in the amp's DMA when playback is cut (6 x 10 ms): written but not yet heard.
constexpr int AMP_QUEUE_MS = 60;
// Every recording starts with a ~100 ms mic start-up burst; it's never sent or measured.
constexpr size_t SETTLE_SAMPLES = mic::SAMPLE_RATE / 10;
// Speech peaks ~1400 of 32767 at arm's length; 16x brings them to ~70% of full scale.
// Applied to the uplink (docs/protocol.md); automatic gain control later.
constexpr float UPLINK_GAIN = 16;
constexpr size_t UPLINK_CHUNK = mic::SAMPLE_RATE / 10;  // 100 ms per frame; the backlog goes out fast

constexpr int WIFI_TIMEOUT_MS = 10000;  // from boot; a cold association normally takes 1-3 s
constexpr int WS_TIMEOUT_MS = 3000;
constexpr int REPLY_TIMEOUT_MS = 15000;  // after release; the AI may call tools first

// Shared between app_main and the uplink task for one turn.
std::atomic<bool> released{false};
std::atomic<int> uplink_result{0};  // 0 running, 1 turn sent, -1 failed
int64_t ws_open_us = 0;
int battery_mv = 0;
char device_id[16];

enum class Outcome { Done, Interrupted, Failed };

void delay_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

int64_t now_us()
{
    return esp_timer_get_time();
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

// Streams one turn from the PSRAM buffer as it grows: first the backlog captured while the
// network came up, then live audio, until the button is released. Turn 1 also connects.
void uplink_task(void *arg)
{
    const int turn = int(reinterpret_cast<intptr_t>(arg));
    bool ok = true;
    if (turn == 1) {
        ok = wifi::wait_connected(WIFI_TIMEOUT_MS) && session::open(WS_TIMEOUT_MS);
        if (ok) {
            ws_open_us = now_us();
            char msg[160];
            snprintf(msg, sizeof(msg),
                     R"({"type":"hello","v":1,"device_id":"%s","firmware":"dev","battery_mv":%d})", device_id,
                     battery_mv);
            ok = session::send_text(msg);
        }
    } else {
        ok = !session::failed();
    }
    if (ok) {
        char msg[48];
        snprintf(msg, sizeof(msg), R"({"type":"turn_start","turn_id":%d})", turn);
        ok = session::send_text(msg);
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
        snprintf(msg, sizeof(msg), R"({"type":"turn_end","turn_id":%d,"audio_ms":%d})", turn,
                 int((sent - SETTLE_SAMPLES) * 1000 / mic::SAMPLE_RATE));
        ok = session::send_text(msg);
    }
    uplink_result = ok ? 1 : -1;
    vTaskDelete(nullptr);
}

// Start the mic for a new turn. Green = audio arriving, red = the mic never delivered.
bool start_listening()
{
    bool ok = mic::start(xTaskGetCurrentTaskHandle()) && ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) > 0;
    led::set(ok ? 0 : 40, ok ? 40 : 0, 0);
    return ok;
}

// Debounced press while waiting for or playing a reply. Call regularly; `held_us` keeps state.
bool interrupt_pressed(int64_t &held_since)
{
    if (!power::ptt_pressed()) {
        held_since = 0;
        return false;
    }
    if (!held_since) {
        held_since = now_us();
    }
    return now_us() - held_since >= INTERRUPT_PRESS_MS * 1000LL;
}

struct ReplyStats {
    int64_t play_us = 0;      // playback started
    int64_t complete_us = 0;  // whole reply had arrived
    int heard_ms = 0;
};

// Wait for the reply and play it while it streams in. A press interrupts: the mic starts at once
// (so no speech is lost), playback fades out, and the server learns how much was heard.
Outcome reply_or_interrupt(ReplyStats &stats)
{
    int64_t held_since = 0;
    const int64_t deadline = now_us() + REPLY_TIMEOUT_MS * 1000LL;
    while (!session::reply_started()) {
        if (interrupt_pressed(held_since)) {
            return start_listening() ? Outcome::Interrupted : Outcome::Failed;
        }
        if (session::failed() || now_us() > deadline) {
            return Outcome::Failed;
        }
        delay_ms(10);
    }

    const int rate = std::max(1, session::reply_sample_rate());
    const size_t prebuffer = size_t(rate) * PREBUFFER_MS / 1000;
    const size_t block = size_t(rate) / 50;  // 20 ms per write
    while (!session::reply_complete() && !session::failed() && session::reply_sample_count() < prebuffer) {
        if (interrupt_pressed(held_since)) {
            return start_listening() ? Outcome::Interrupted : Outcome::Failed;
        }
        delay_ms(5);
    }

    // amp::write blocks, which paces this loop to real time.
    bool speaking = amp::begin(rate);
    led::set(20, 0, 20);  // purple: playing the reply
    stats.play_us = now_us();
    int64_t last_audio_us = stats.play_us;
    size_t played = 0;
    Outcome outcome = Outcome::Done;
    while (true) {
        if (interrupt_pressed(held_since)) {
            outcome = start_listening() ? Outcome::Interrupted : Outcome::Failed;
            break;
        }
        bool done = session::reply_complete();  // read before the count: then the count is final
        size_t available = session::reply_sample_count();
        if (done && !stats.complete_us) {
            stats.complete_us = now_us();
        }
        if (played < available) {
            size_t n = std::min(block, available - played);
            if (speaking) {
                amp::write(session::reply_samples() + played, n, 1.0f, done && played + n == available);
            }
            played += n;
            last_audio_us = now_us();
        } else if (done || session::failed() || now_us() - last_audio_us > REPLY_STALL_MS * 1000LL) {
            break;  // finished, or the link dropped / went quiet mid-reply
        } else {
            delay_ms(5);  // reply running behind playback
        }
    }

    const char *reason = "done";
    stats.heard_ms = int(played * 1000 / rate);
    if (outcome != Outcome::Done) {
        reason = "interrupted";
        // Audio still in the amp's queue hasn't been heard yet. Fade out from here instead of cutting.
        stats.heard_ms = std::max(0, stats.heard_ms - AMP_QUEUE_MS);
        size_t n = std::min(block, session::reply_sample_count() - played);
        if (speaking && n) {
            amp::write(session::reply_samples() + played, n, 1.0f, true);
        }
    }
    if (speaking) {
        amp::end();
    }
    char msg[112];
    snprintf(msg, sizeof(msg), R"({"type":"playback_stopped","reply_id":%d,"played_ms":%d,"reason":"%s"})",
             session::reply_id(), stats.heard_ms, reason);
    session::send_text(msg);
    if (outcome == Outcome::Done) {
        led::set(0, 0, 0);
    }
    return outcome;
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
    int64_t boot_us = now_us();

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
        // audio buffered in PSRAM until the network is up.
        bool listening = start_listening();
        int64_t listen_us = now_us();

        // Then the network, in the background. Wi-Fi stays up across runs on USB.
        battery_mv = battery::read_mv();
        wifi::start();
        ws_open_us = 0;

        Outcome outcome = Outcome::Failed;
        for (int turn = 1; listening; turn++) {
            int64_t turn_us = now_us();
            session::expect_turn(turn);  // before turn_start goes out: replies to older turns are dropped
            released = false;
            uplink_result = 0;
            xTaskCreate(uplink_task, "uplink", 8192, reinterpret_cast<void *>(intptr_t(turn)), 5, nullptr);

            // Push-to-talk: record while S1 is held. It's held already: from the press that
            // powered us on, or the one that interrupted the last reply.
            int released_ms = 0;
            while (released_ms < RELEASE_MS && mic::sample_count() < mic::capacity()) {
                delay_ms(10);
                bool min_done = turn > 1 || now_us() - turn_us >= MIN_RECORD_MS * 1000LL;
                released_ms = (!power::ptt_pressed() && min_done) ? released_ms + 10 : 0;
            }
            int64_t release_us = now_us();
            delay_ms(RELEASE_TAIL_MS);
            mic::stop();
            released = true;
            led::set(30, 20, 0);  // amber: waiting for the reply

            while (uplink_result == 0) {
                delay_ms(10);
            }
            if (turn == 1) {
                log_span("uplink", SETTLE_SAMPLES, mic::sample_count());
            }
            ReplyStats stats;
            outcome = uplink_result == 1 ? reply_or_interrupt(stats) : Outcome::Failed;

            auto ms = [](int64_t us) { return us > 0 ? int(us / 1000) : -1; };
            if (outcome == Outcome::Failed) {
                ESP_LOGE(TAG, "turn %d: no reply (%s)", turn,
                         uplink_result == 1 ? "server didn't answer" : "couldn't reach the server");
                break;
            }
            ESP_LOGI(TAG, "turn %d: reply %d %s after %d ms heard; release -> first audio %d, playing %d, all arrived %d ms",
                     turn, session::reply_id(), outcome == Outcome::Interrupted ? "interrupted" : "done",
                     stats.heard_ms,
                     session::reply_first_audio_us() ? ms(session::reply_first_audio_us() - release_us) : -1,
                     stats.play_us ? ms(stats.play_us - release_us) : -1,
                     stats.complete_us ? ms(stats.complete_us - release_us) : -1);
            if (outcome == Outcome::Done) {
                break;
            }
            // Interrupted: the mic is already running for the next turn.
        }
        if (outcome == Outcome::Failed) {
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
