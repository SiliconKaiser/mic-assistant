#pragma once

#include <cstdint>

namespace wifi {

// Start joining the network from secrets.h in the background. Later calls do nothing, or
// rejoin after stop().
void start();

// Leave the network cleanly (the access point is told), and stop rejoining. Call before
// cutting power: an access point that still thinks we're associated can delay the next join.
void stop();

// Wait until connected with an IP address. False on timeout.
bool wait_connected(int timeout_ms);

// esp_timer time when the IP address arrived (us since app start), or 0 while not connected.
int64_t connected_us();

// esp_timer times (us since app start) of each stage of the first connection, 0 if not reached.
struct Timing {
    int64_t start;       // start() called
    int64_t driver_up;   // driver initialized and started (NVS, netif, PHY calibration)
    int64_t associated;  // joined the access point (after scanning and authentication)
    int64_t got_ip;      // IP address assigned (DHCP, or immediately with a static IP)
    int kicks;           // join attempts restarted by the watchdog (see wifi.cpp)
};
Timing timing();

// True if this boot joined via the cached access point (channel + BSSID) instead of a scan.
bool used_cached_ap();

// Resolve `ip`'s MAC address (ARP) before the first TCP packet goes out, re-asking every 50 ms.
// lwIP itself only re-sends a pending ARP request once a second, so one missed request (a host
// whose Wi-Fi is in power save can miss it) costs ~1 s. Returns the number of requests that went
// unanswered (0 = the first one worked), or -1 if not resolved within timeout_ms. Hostnames
// (not IPs) return 0 without doing anything.
int warm_arp(const char *ip, int timeout_ms);

}  // namespace wifi
