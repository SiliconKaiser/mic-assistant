// Skeleton: hold power, report boot time and battery, blink, power off.
// Audio, Wi-Fi and the server protocol come next.

#include "battery.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power.h"

static const char *TAG = "main";

void app_main(void)
{
    power_init();
    int64_t boot_us = esp_timer_get_time();  // time since the chip came out of reset

    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "board=%s  app_main at %lld ms after reset", BOARD_NAME, boot_us / 1000);
    ESP_LOGI(TAG, "battery %d mV, PTT %s", battery_read_mv(), power_ptt_pressed() ? "held" : "released");

    gpio_config_t led = {.pin_bit_mask = 1ULL << PIN_USER_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&led);
    for (int i = 0; i < 6; i++) {
        gpio_set_level(PIN_USER_LED, i % 2);
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    power_off();
}
