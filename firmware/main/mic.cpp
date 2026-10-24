#include "mic.h"

#include <algorithm>
#include <atomic>

#include "board.h"
#include "driver/i2s_pdm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

namespace mic {
namespace {

const char *TAG = "mic";

constexpr size_t MAX_SECONDS = 30;
constexpr size_t CAPACITY = SAMPLE_RATE * MAX_SECONDS;  // samples: 960 KB of PSRAM
// One DMA frame per 10 ms. Smaller frames deliver the first audio sooner.
constexpr uint32_t DMA_FRAME = SAMPLE_RATE / 100;

// DC-blocking high-pass. Right after power-up the PDM output carries a large offset that
// drifts for seconds (bench, 2026-10-24: mean ~3800 falling to ~2000 over 1.5 s, against
// ~45 peak for real silence), and the S3 has no hardware PDM high-pass. Since we listen
// within ~75 ms of the press, every recording starts inside that drift. 80 Hz cut-off: speech
// starts around 100 Hz.
constexpr float HPF_CUTOFF_HZ = 80;
constexpr float HPF_A = 1 - 2 * 3.14159265f * HPF_CUTOFF_HZ / SAMPLE_RATE;

i2s_chan_handle_t rx = nullptr;
int16_t *buffer = nullptr;
SemaphoreHandle_t done = nullptr;
TaskHandle_t notify_task = nullptr;
std::atomic<size_t> count{0};
std::atomic<int64_t> first_us{0};
std::atomic<bool> running{false};

// y[n] = x[n] - x[n-1] + a * y[n-1], in place.
void high_pass(int16_t *s, size_t n)
{
    static float x_prev = 0, y_prev = 0;
    for (size_t i = 0; i < n; i++) {
        float x = s[i];
        float y = x - x_prev + HPF_A * y_prev;
        x_prev = x;
        y_prev = y;
        s[i] = int16_t(std::clamp(y, -32768.0f, 32767.0f));
    }
}

void capture_task(void *)
{
    while (running) {
        size_t room = CAPACITY - count;
        if (room == 0) {
            break;  // full: keep the first MAX_SECONDS
        }
        size_t want = std::min<size_t>(DMA_FRAME, room) * sizeof(int16_t);
        size_t got = 0;
        // Short timeout so stop() never waits long for this loop to notice.
        i2s_channel_read(rx, buffer + count, want, &got, pdMS_TO_TICKS(100));
        if (got == 0) {
            continue;
        }
        if (first_us == 0) {
            first_us = esp_timer_get_time();
            xTaskNotifyGive(notify_task);
        }
        high_pass(buffer + count, got / sizeof(int16_t));
        count += got / sizeof(int16_t);
    }
    xSemaphoreGive(done);
    vTaskDelete(nullptr);
}

void release_channel()
{
    if (rx) {
        i2s_channel_disable(rx);
        i2s_del_channel(rx);
        rx = nullptr;
    }
}

}  // namespace

bool start(TaskHandle_t notify)
{
    // Audio buffers belong in PSRAM; internal RAM is too small and needed elsewhere.
    if (!buffer) {
        buffer = static_cast<int16_t *>(heap_caps_malloc(CAPACITY * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    }
    if (!buffer) {
        ESP_LOGE(TAG, "no PSRAM for the buffer");
        return false;
    }

    // PDM RX only works on I2S port 0 on the ESP32-S3; the amp gets port 1.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = DMA_FRAME;
    if (i2s_new_channel(&chan_cfg, nullptr, &rx) != ESP_OK) {
        ESP_LOGE(TAG, "no I2S channel");
        return false;
    }

    // SEL is tied to GND, so the mic is on the left slot: the mono default.
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.clk = board::PIN_MIC_CLK, .din = board::PIN_MIC_DAT},
    };
    if (i2s_channel_init_pdm_rx_mode(rx, &pdm_cfg) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
        ESP_LOGE(TAG, "PDM RX setup failed");
        i2s_del_channel(rx);
        rx = nullptr;
        return false;
    }

    notify_task = notify;
    count = 0;
    first_us = 0;
    running = true;
    done = xSemaphoreCreateBinary();
    // Above app_main's priority so a busy main loop can't starve capture.
    xTaskCreate(capture_task, "mic", 4096, nullptr, 10, nullptr);
    return true;
}

void stop()
{
    if (!running) {
        return;
    }
    running = false;
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    done = nullptr;
    release_channel();
}

const int16_t *samples()
{
    return buffer;
}

size_t sample_count()
{
    return count;
}

size_t capacity()
{
    return CAPACITY;
}

int64_t first_audio_us()
{
    return first_us;
}

}  // namespace mic
