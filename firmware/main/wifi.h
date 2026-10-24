#pragma once

#include <cstdint>

namespace wifi {

// Start joining the network from secrets.h in the background. Later calls do nothing.
void start();

// Wait until connected with an IP address. False on timeout.
bool wait_connected(int timeout_ms);

// esp_timer time when the IP address arrived (us since app start), or 0 while not connected.
int64_t connected_us();

}  // namespace wifi
