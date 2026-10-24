#include "wifi.h"

#include <atomic>
#include <cstring>

#include "credentials.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

// TODO(latency): association from cold takes seconds. Cache the access point's channel and
// BSSID in NVS and use a static IP to cut it to a few hundred ms (docs/protocol.md).

namespace wifi {
namespace {

const char *TAG = "wifi";
constexpr EventBits_t GOT_IP = BIT0;

EventGroupHandle_t events = nullptr;
std::atomic<int64_t> got_ip_us{0};

void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, GOT_IP);
        got_ip_us = 0;
        auto *d = static_cast<wifi_event_sta_disconnected_t *>(data);
        ESP_LOGW(TAG, "not connected (reason %d), retrying", d->reason);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        got_ip_us = esp_timer_get_time();
        xEventGroupSetBits(events, GOT_IP);
    }
}

}  // namespace

void start()
{
    if (events) {
        return;
    }
    events = xEventGroupCreate();

    // The Wi-Fi driver needs NVS (its PHY calibration lives there).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr);

    // Credentials come from the firmware image, so don't rewrite them to flash on every boot.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wifi_config_t cfg = {};
    strlcpy(reinterpret_cast<char *>(cfg.sta.ssid), secrets::WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy(reinterpret_cast<char *>(cfg.sta.password), secrets::WIFI_PASSWORD, sizeof(cfg.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
    // The device is only on for one exchange: latency matters more than radio power save.
    esp_wifi_set_ps(WIFI_PS_NONE);
}

bool wait_connected(int timeout_ms)
{
    return xEventGroupWaitBits(events, GOT_IP, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms)) & GOT_IP;
}

int64_t connected_us()
{
    return got_ip_us;
}

}  // namespace wifi
