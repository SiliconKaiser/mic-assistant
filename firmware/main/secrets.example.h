// Copy to secrets.h (ignored by git) and fill in. See docs/protocol.md "Wi-Fi and credentials".
#pragma once

namespace secrets {

// Must be a 2.4 GHz network: the ESP32-S3 has no 5 GHz radio. (A dual-band network with one
// name is fine; the device joins its 2.4 GHz side.)
inline constexpr const char *WIFI_SSID = "your-network";
inline constexpr const char *WIFI_PASSWORD = "your-password";

// The home server running server/mic_server.py. An IP for now; the server moves from the Mac to
// a Linux box later, so an mDNS name (<host>.local) is the plan.
inline constexpr const char *SERVER_HOST = "192.168.4.101";
inline constexpr int SERVER_PORT = 8765;

// Must match MIC_DEVICE_TOKEN in the server's env file (local/server.env).
inline constexpr const char *DEVICE_TOKEN = "change-me";

}  // namespace secrets
