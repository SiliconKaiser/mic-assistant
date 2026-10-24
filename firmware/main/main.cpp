// Skeleton: hold power, report boot time and battery, blink, power off.
// Audio, Wi-Fi and the server protocol come next.

#include "battery.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "power.h"

namespace {

const char *TAG = "main";

}  // namespace

extern "C" void app_main()
{
    power::init();
    int64_t boot_us = esp_timer_get_time();  // time since the chip came out of reset

    // Boot indicator: blue as soon as our code runs. Filming the latch LED and this one in
    // slow motion measures press-to-code time (Arduino baseline: ~100-170 ms).
    led::init();
    led::set(0, 0, 40);
    int64_t led_us = esp_timer_get_time();

    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "board=%s  app_main at %lld ms, LED on at %lld ms after reset", board::NAME, boot_us / 1000,
             led_us / 1000);
    ESP_LOGI(TAG, "battery %d mV (latch %s), PTT %s", battery::read_mv(), power::vsw_present() ? "on" : "off",
             power::ptt_pressed() ? "held" : "released");

    gpio_config_t led = {.pin_bit_mask = 1ULL << board::PIN_USER_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&led);
    for (int i = 0; i < 6; i++) {
        gpio_set_level(board::PIN_USER_LED, i % 2);
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    led::set(0, 0, 0);
    power::off();
}
