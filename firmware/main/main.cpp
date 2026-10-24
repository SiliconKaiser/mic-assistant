// Skeleton push-to-talk flow: hold power, start listening as early as possible, record while
// the button is held, log a summary, power off. Wi-Fi and the server protocol come next.

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "battery.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "mic.h"
#include "power.h"

namespace {

const char *TAG = "main";

// Skeleton behavior: always record at least this long, so a tap (or a reset over USB) still
// records something, and the summary prints after USB has re-enumerated (~1.5 s).
constexpr int MIN_RECORD_MS = 2000;
// The button must read released this long before recording stops (contact bounce).
constexpr int RELEASE_MS = 50;
// The PDM decimation filter settles during the first ~100 ms; leave that out of the levels.
constexpr size_t SETTLE_SAMPLES = mic::SAMPLE_RATE / 10;

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

void log_levels()
{
    size_t n = mic::sample_count();
    const size_t half_s = mic::SAMPLE_RATE / 2;
    ESP_LOGI(TAG, "recorded %d ms (levels of 32767)", int(n * 1000 / mic::SAMPLE_RATE));
    log_span("start", 0, SETTLE_SAMPLES);
    log_span("first", SETTLE_SAMPLES, SETTLE_SAMPLES + half_s);
    log_span("last", n > half_s ? n - half_s : 0, n);
    log_span("all", SETTLE_SAMPLES, n);
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
    int64_t led_us = esp_timer_get_time();

    esp_log_level_set("*", ESP_LOG_INFO);
    for (int run = 1;; run++) {
        // Listen before anything slow (Wi-Fi, later): the goal is under 300 ms from press to
        // listening, with audio buffered in PSRAM until the network is up. Green = audio arriving.
        bool listening =
            mic::start(xTaskGetCurrentTaskHandle()) && ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) > 0;
        led::set(listening ? 0 : 40, listening ? 40 : 0, 0);  // red if the mic never delivered

        // Push-to-talk: record while S1 is held. On battery it's still held from the press that
        // powered us on.
        int64_t start_us = esp_timer_get_time();
        int released_ms = 0;
        while (listening && released_ms < RELEASE_MS && mic::sample_count() < mic::capacity()) {
            delay_ms(10);
            bool min_done = esp_timer_get_time() - start_us >= MIN_RECORD_MS * 1000LL;
            released_ms = (!power::ptt_pressed() && min_done) ? released_ms + 10 : 0;
        }
        mic::stop();
        led::set(0, 0, 0);

        // Logged at the end because USB drops everything from the first ~1.5 s after a reset.
        ESP_LOGI(TAG, "run %d  board=%s  since app start: app_main %lld ms, LED %lld ms, first audio %lld ms", run,
                 board::NAME, boot_us / 1000, led_us / 1000, mic::first_audio_us() / 1000);
        if (listening) {
            log_levels();
        } else {
            ESP_LOGE(TAG, "mic delivered no audio");
        }
        ESP_LOGI(TAG, "battery %d mV", battery::read_mv());

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
