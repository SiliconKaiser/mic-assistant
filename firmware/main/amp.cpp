#include "amp.h"

#include <algorithm>

#include "board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power.h"

namespace amp {
namespace {

const char *TAG = "amp";

constexpr uint32_t MAX_RATE = 48000;
constexpr size_t MAX_CHUNK = MAX_RATE / 100;  // writes and DMA frames are 10 ms
constexpr int DMA_DESCS = 6;
// Abrupt starts and stops click; 20 ms fades don't, and still sound instant. (FADE_DIV = 1/20 ms)
constexpr uint32_t FADE_DIV = 50;
// Cap peaks at ~70% of full scale. With the 4 ohm speaker this bounds the current spikes on the
// battery supply (reset risk); with the 8 ohm 1 W one it also keeps the speaker in its rating.
constexpr float LIMIT = 23000;
// Zeros before and after the audio, so SD switches while the output is silent: toggling SD
// with audio (or stale DMA data) playing pops.
constexpr int SILENCE_CHUNKS = 3;

void write(i2s_chan_handle_t tx, const int16_t *s, size_t n)
{
    size_t written = 0;
    i2s_channel_write(tx, s, n * sizeof(int16_t), &written, portMAX_DELAY);
}

// Static, not on the stack: at 48 kHz these are ~1 KB each, and app_main's stack overflowed
// with them there (2026-10-24). play() is only ever called from one task.
const int16_t zeros[MAX_CHUNK] = {};
int16_t chunk[MAX_CHUNK];

void write_silence(i2s_chan_handle_t tx, size_t len)
{
    for (int i = 0; i < SILENCE_CHUNKS; i++) {
        write(tx, zeros, len);
    }
}

void set_sd(bool on)
{
    gpio_set_level(board::PIN_AMP_SD, on);
    gpio_config_t sd = {.pin_bit_mask = 1ULL << board::PIN_AMP_SD, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&sd);
}

}  // namespace

bool play(const int16_t *samples, size_t n, float gain, uint32_t sample_rate)
{
    // The amp's supply only exists while the latch is on; never drive its pins otherwise.
    if (!power::vsw_present()) {
        ESP_LOGW(TAG, "amp has no power (latch off); not playing");
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (sample_rate < 8000 || sample_rate > MAX_RATE) {
        ESP_LOGE(TAG, "unsupported sample rate %lu", (unsigned long)sample_rate);
        return false;
    }
    const size_t chunk_len = sample_rate / 100;
    const size_t fade = sample_rate / FADE_DIV;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESCS;
    chan_cfg.dma_frame_num = chunk_len;
    chan_cfg.auto_clear = true;  // send zeros, not stale data, if writes fall behind
    i2s_chan_handle_t tx = nullptr;
    if (i2s_new_channel(&chan_cfg, &tx, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "no I2S channel");
        return false;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                     .bclk = board::PIN_I2S_BCLK,
                     .ws = board::PIN_I2S_LRCLK,
                     .dout = board::PIN_I2S_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    // Mono defaults to the left slot only. Send both: the amp plays left or (L+R)/2 depending
    // on SD's voltage, and this way either one is the full signal.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    if (i2s_channel_init_std_mode(tx, &std_cfg) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed");
        i2s_del_channel(tx);
        power::release_amp_pins();
        return false;
    }

    write_silence(tx, chunk_len);
    set_sd(true);

    for (size_t pos = 0; pos < n; pos += chunk_len) {
        size_t len = std::min(chunk_len, n - pos);
        for (size_t i = 0; i < len; i++) {
            size_t k = pos + i;
            float level = std::min({1.0f, float(k) / fade, float(n - 1 - k) / fade});
            chunk[i] = int16_t(std::clamp(samples[k] * gain * level, -LIMIT, LIMIT));
        }
        write(tx, chunk, len);
    }

    write_silence(tx, chunk_len);
    // Writes return once queued; wait for the DMA to drain before shutting the amp down.
    vTaskDelay(pdMS_TO_TICKS(DMA_DESCS * 10 + 10));
    set_sd(false);

    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    power::release_amp_pins();
    return true;
}

}  // namespace amp
