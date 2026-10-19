#include "power.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

void power_init(void)
{
    gpio_config_t off = {
        .pin_bit_mask = 1ULL << PIN_LATCH_OFF,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&off);
#if LATCH_SELF_HOLDING
    gpio_set_level(PIN_LATCH_OFF, 0);  // latch holds itself; just make sure OFF is idle
#else
    gpio_set_level(PIN_LATCH_OFF, 1);  // HOLD pin: keep power on
#endif

    gpio_config_t ptt = {
        .pin_bit_mask = 1ULL << PIN_PTT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&ptt);
}

bool power_ptt_pressed(void)
{
    return gpio_get_level(PIN_PTT) == 0;
}

void power_off(void)
{
    ESP_LOGI(TAG, "powering off");
    // Don't cut power while the button is still held; release first.
    while (power_ptt_pressed()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#if LATCH_SELF_HOLDING
    gpio_set_level(PIN_LATCH_OFF, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_LATCH_OFF, 0);
#else
    gpio_set_level(PIN_LATCH_OFF, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
    ESP_LOGW(TAG, "still running after power-off: on USB power");
}
