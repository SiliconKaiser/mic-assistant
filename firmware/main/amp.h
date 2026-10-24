#pragma once

#include <cstddef>
#include <cstdint>

namespace amp {

// Play mono 16-bit samples at `sample_rate` (8-48 kHz) through the MAX98357A on I2S port 1. Applies `gain`, a peak limit
// (see LIMIT), and 20 ms fades. Blocks until done. Plays nothing and returns false if
// the amp has no power (VSW off: on USB with the latch off).
bool play(const int16_t *samples, size_t n, float gain, uint32_t sample_rate);

}  // namespace amp
