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
#include "esp_netif_net_stack.h"
#include "lwip/etharp.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"
#include "nvs.h"
#include "nvs_flash.h"

// Fast connect. Measured cold boots (2026-10-24, before these): scanning every channel took
// ~0.7 s and DHCP 1-1.5 s. So:
//  - A static IP (secrets::DEVICE_IP) skips DHCP. With it, "connected" means associated: the
//    address is usable as soon as the link is up.
//  - The access point's channel and BSSID are cached in NVS after each successful join, and the
//    next boot tries only that one. If it's gone, the cache is dropped and we scan again.

namespace wifi {
namespace {

const char *TAG = "wifi";
constexpr EventBits_t GOT_IP = BIT0;
constexpr EventBits_t LEFT = BIT1;
constexpr const char *NVS_NAMESPACE = "wifi_fast";
constexpr const char *NVS_KEY = "ap";

struct CachedAp {
    uint8_t bssid[6];
    uint8_t channel;
};

EventGroupHandle_t events = nullptr;
esp_netif_t *netif = nullptr;
std::atomic<int64_t> got_ip_us{0};
Timing first{};  // first connection after boot
bool using_cache = false;
std::atomic<bool> stopping{false};
bool static_ip = false;

bool load_cache(CachedAp &ap)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = sizeof(ap);
    bool ok = nvs_get_blob(h, NVS_KEY, &ap, &len) == ESP_OK && len == sizeof(ap);
    nvs_close(h);
    return ok;
}

void store_cache(const CachedAp *ap)  // nullptr erases it
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (ap) {
        nvs_set_blob(h, NVS_KEY, ap, sizeof(*ap));
    } else {
        nvs_erase_key(h, NVS_KEY);
    }
    nvs_commit(h);
    nvs_close(h);
}

// Remember the access point we joined, but only write flash when it changed.
void remember_ap()
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) {
        return;
    }
    CachedAp now = {};
    memcpy(now.bssid, info.bssid, sizeof(now.bssid));
    now.channel = info.primary;
    CachedAp old;
    if (!load_cache(old) || memcmp(&old, &now, sizeof(now)) != 0) {
        store_cache(&now);
    }
}

// Back to a plain scan for the SSID, e.g. when the cached access point isn't there anymore.
void forget_ap()
{
    using_cache = false;
    store_cache(nullptr);
    wifi_config_t cfg;
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    cfg.sta.channel = 0;
    cfg.sta.bssid_set = false;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

void mark_connected()
{
    got_ip_us = esp_timer_get_time();
    if (!first.got_ip) {
        first.got_ip = got_ip_us;
    }
    xEventGroupSetBits(events, GOT_IP);
}

void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        if (!first.associated) {
            first.associated = esp_timer_get_time();
        }
        // The default handler has already brought the netif up with the static address.
        if (static_ip) {
            mark_connected();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, GOT_IP);
        got_ip_us = 0;
        if (stopping) {
            xEventGroupSetBits(events, LEFT);
            return;
        }
        auto *d = static_cast<wifi_event_sta_disconnected_t *>(data);
        ESP_LOGW(TAG, "not connected (reason %d), retrying", d->reason);
        if (using_cache) {
            forget_ap();  // the cached access point may have moved channel or been replaced
        }
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        mark_connected();
    }
}

// Runs on the lwIP thread (core locking is off, so lwIP may only be called from there).
struct ArpJob {
    struct netif *nif;
    ip4_addr_t ip;
    bool resolved;
    SemaphoreHandle_t done;
};

void arp_step(void *arg)
{
    auto *job = static_cast<ArpJob *>(arg);
    struct eth_addr *eth;
    const ip4_addr_t *found;
    job->resolved = etharp_find_addr(job->nif, &job->ip, &eth, &found) >= 0;
    if (!job->resolved) {
        etharp_request(job->nif, &job->ip);
    }
    xSemaphoreGive(job->done);
}

void configure_static_ip()
{
    esp_netif_ip_info_t ip = {};
    ip.ip.addr = ipaddr_addr(secrets::DEVICE_IP);
    ip.gw.addr = ipaddr_addr(secrets::GATEWAY);
    ip.netmask.addr = ipaddr_addr(secrets::NETMASK);
    esp_netif_dhcpc_stop(netif);
    esp_netif_set_ip_info(netif, &ip);
    if (secrets::DNS_SERVER[0]) {
        esp_netif_dns_info_t dns = {};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = ipaddr_addr(secrets::DNS_SERVER);
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    static_ip = true;
}

}  // namespace

void start()
{
    if (events) {
        if (stopping.exchange(false)) {
            esp_wifi_connect();
        }
        return;
    }
    events = xEventGroupCreate();
    first.start = esp_timer_get_time();

    // The Wi-Fi driver needs NVS (its PHY calibration lives there), and so does the AP cache.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    netif = esp_netif_create_default_wifi_sta();
    if (secrets::DEVICE_IP[0]) {
        configure_static_ip();
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr);

    // Credentials come from the firmware image, so don't rewrite them to flash on every boot.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wifi_config_t cfg = {};
    strlcpy(reinterpret_cast<char *>(cfg.sta.ssid), secrets::WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy(reinterpret_cast<char *>(cfg.sta.password), secrets::WIFI_PASSWORD, sizeof(cfg.sta.password));
    CachedAp ap;
    if (load_cache(ap)) {
        memcpy(cfg.sta.bssid, ap.bssid, sizeof(ap.bssid));
        cfg.sta.bssid_set = true;
        cfg.sta.channel = ap.channel;
        using_cache = true;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
    // The device is only on for one exchange: latency matters more than radio power save.
    esp_wifi_set_ps(WIFI_PS_NONE);
    first.driver_up = esp_timer_get_time();
}

void stop()
{
    if (!events || stopping) {
        return;
    }
    stopping = true;
    xEventGroupClearBits(events, LEFT);
    if (esp_wifi_disconnect() == ESP_OK) {
        // Give the driver a moment to actually send the deauthentication frame.
        xEventGroupWaitBits(events, LEFT, pdFALSE, pdFALSE, pdMS_TO_TICKS(200));
    }
}

bool wait_connected(int timeout_ms)
{
    bool ok = xEventGroupWaitBits(events, GOT_IP, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms)) & GOT_IP;
    if (ok) {
        remember_ap();  // here, not in the event handler: flash writes don't belong in the event task
    }
    return ok;
}

int64_t connected_us()
{
    return got_ip_us;
}

Timing timing()
{
    return first;
}

bool used_cached_ap()
{
    return using_cache;
}

int warm_arp(const char *ip, int timeout_ms)
{
    ArpJob job = {};
    if (!netif || !ip4addr_aton(ip, &job.ip)) {
        return 0;
    }
    job.nif = static_cast<struct netif *>(esp_netif_get_netif_impl(netif));
    job.done = xSemaphoreCreateBinary();
    int unanswered = -1;
    int64_t deadline = esp_timer_get_time() + timeout_ms * 1000LL;
    for (int asked = 0; esp_timer_get_time() < deadline; asked++) {
        if (tcpip_callback(arp_step, &job) != ERR_OK) {
            break;
        }
        xSemaphoreTake(job.done, portMAX_DELAY);
        if (job.resolved) {
            unanswered = asked > 0 ? asked - 1 : 0;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vSemaphoreDelete(job.done);
    return unanswered;
}

}  // namespace wifi
