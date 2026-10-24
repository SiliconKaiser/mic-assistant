// Pulls in secrets.h, and fails the build with a useful message if it's missing or unfilled.
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing firmware/main/secrets.h: copy secrets.example.h to secrets.h and fill it in"
#endif

namespace credentials_check {

constexpr bool same(const char *a, const char *b)
{
    return *a == *b && (*a == '\0' || same(a + 1, b + 1));
}

static_assert(!same(secrets::WIFI_SSID, "your-network"), "Set WIFI_SSID in firmware/main/secrets.h");
static_assert(!same(secrets::DEVICE_TOKEN, "change-me"), "Set DEVICE_TOKEN in firmware/main/secrets.h");

}  // namespace credentials_check
