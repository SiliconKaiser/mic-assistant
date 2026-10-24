#include "power.h"

#include "battery.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace power {
namespace {

const char *TAG = "power";

// A pack is flat well before 4.0 V. With the latch off, VSW reads under ~0.4 V
// (leftover charge bleeding off through the divider), so 2 V cleanly separates the two.
constexpr int VSW_PRESENT_MV = 2000;

constexpr int OFF_PULSE_MS = 50;
constexpr int OFF_ATTEMPTS = 3;
// After the latch turns off on USB, VSW takes a few hundred ms to fall below VSW_PRESENT_MV.
constexpr int OFF_SETTLE_MS = 1000;

void delay_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// The amp runs from the 5V rail, which only has power while the latch is on: D1 stops USB
// from feeding it. Driving the amp's inputs high while it's unpowered back-powers it through
// its input protection diodes. On the bench that made it half-work and crackle, and it
// stresses the chip. So these pins float until audio code has checked vsw_present().
// Floating SD is safe: R4 holds it low (amp shut down).
// GPIO43 is UART0 TX by default, and the ROM prints its boot banner there before any app
// code runs. That's brief, and stopping it means burning an eFuse, so it's accepted.
void release_amp_pins()
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << board::PIN_I2S_BCLK) | (1ULL << board::PIN_I2S_LRCLK) |
                        (1ULL << board::PIN_I2S_DOUT) | (1ULL << board::PIN_AMP_SD),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

}  // namespace

void init()
{
    // Self-holding latch (prototype): OFF must idle low; high cuts power, and Pololu warns
    // that holding it high interferes with the latch's other inputs.
    // HOLD-pin latch (PCB): drive high as early as possible, or power drops on button release.
    gpio_set_level(board::PIN_LATCH_OFF, board::LATCH_SELF_HOLDING ? 0 : 1);
    gpio_config_t off = {
        .pin_bit_mask = 1ULL << board::PIN_LATCH_OFF,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&off);

    // PTT reads S1 through D2 from latch pin A. When the supply sags, A's level drops and
    // PTT reads "pressed" with nobody touching it (seen on the bench with a meter's uA range
    // in series with the cells). Expect the same from a nearly flat pack.
    gpio_config_t ptt = {
        .pin_bit_mask = 1ULL << board::PIN_PTT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&ptt);

    release_amp_pins();
}

bool ptt_pressed()
{
    return gpio_get_level(board::PIN_PTT) == 0;
}

bool vsw_present()
{
    return battery::read_mv() > VSW_PRESENT_MV;
}

void off()
{
    // Logs from the first ~1.5 s after a reset are usually lost on USB: the port re-enumerates
    // and the host reopens it too late. So repeat the state that matters here.
    ESP_LOGI(TAG, "powering off (VSW %d mV)", battery::read_mv());
    // While S1 is held, latch pin A is low, which turns the latch straight back on.
    while (ptt_pressed()) {
        delay_ms(10);
    }

    if constexpr (board::LATCH_SELF_HOLDING) {
        // The latch once ignored an OFF pulse on the bench, and a missed pulse leaves the
        // device on, draining the cells. So check whether VSW actually dropped, and retry.
        for (int attempt = 1; attempt <= OFF_ATTEMPTS; attempt++) {
            gpio_set_level(board::PIN_LATCH_OFF, 1);
            delay_ms(OFF_PULSE_MS);
            gpio_set_level(board::PIN_LATCH_OFF, 0);

            // On battery we're dead by now. Still running: on USB, or the pulse was missed.
            for (int waited = 0; waited < OFF_SETTLE_MS; waited += 50) {
                if (!vsw_present()) {
                    ESP_LOGW(TAG, "latch is off; still running on USB power");
                    return;
                }
                delay_ms(50);
            }
            ESP_LOGW(TAG, "latch ignored OFF pulse %d of %d", attempt, OFF_ATTEMPTS);
        }
        ESP_LOGE(TAG, "latch still on after %d OFF pulses", OFF_ATTEMPTS);
    } else {
        gpio_set_level(board::PIN_LATCH_OFF, 0);
        delay_ms(OFF_PULSE_MS);
        ESP_LOGW(TAG, "still running after power-off: on USB power");
    }
}

}  // namespace power
