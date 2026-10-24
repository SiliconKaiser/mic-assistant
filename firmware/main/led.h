#pragma once

#include <cstdint>

namespace led {

// Status NeoPixel. Call init() once; set() blocks until the color is sent (~30 us).
void init();
void set(uint8_t r, uint8_t g, uint8_t b);

}  // namespace led
