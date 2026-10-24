#include "led.h"

#include "board.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

// One WS2812-style NeoPixel, driven by the RMT peripheral's bytes encoder: each bit is one
// RMT symbol (high then low), sent MSB first in G, R, B order.

namespace led {
namespace {

const char *TAG = "led";

constexpr uint32_t RESOLUTION_HZ = 10'000'000;  // 100 ns per tick
// Bit timings in ticks. 0.4/0.8 us high works for both WS2812B and SK6812.
constexpr uint16_t T0H = 4, T0L = 8, T1H = 8, T1L = 4;

rmt_channel_handle_t channel = nullptr;
rmt_encoder_handle_t encoder = nullptr;

}  // namespace

void init()
{
    rmt_tx_channel_config_t ch_cfg = {
        .gpio_num = board::PIN_LED_DIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RESOLUTION_HZ,
        .mem_block_symbols = 48,  // ESP32-S3 minimum; one pixel needs 24
        .trans_queue_depth = 1,
    };
    if (rmt_new_tx_channel(&ch_cfg, &channel) != ESP_OK) {
        ESP_LOGE(TAG, "no RMT channel");
        return;
    }

    // rmt_symbol_word_t is a union of bitfields, which C++ designated initializers can't
    // reach, so fill it field by field.
    rmt_bytes_encoder_config_t enc_cfg = {};
    enc_cfg.bit0.level0 = 1;
    enc_cfg.bit0.duration0 = T0H;
    enc_cfg.bit0.level1 = 0;
    enc_cfg.bit0.duration1 = T0L;
    enc_cfg.bit1.level0 = 1;
    enc_cfg.bit1.duration0 = T1H;
    enc_cfg.bit1.level1 = 0;
    enc_cfg.bit1.duration1 = T1L;
    enc_cfg.flags.msb_first = 1;
    rmt_new_bytes_encoder(&enc_cfg, &encoder);

    rmt_enable(channel);
}

void set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!channel || !encoder) {
        return;
    }
    const uint8_t grb[3] = {g, r, b};
    rmt_transmit_config_t tx_cfg = {};  // line idles low afterward, which latches the color
    rmt_transmit(channel, encoder, grb, sizeof(grb), &tx_cfg);
    rmt_tx_wait_all_done(channel, -1);
}

}  // namespace led
