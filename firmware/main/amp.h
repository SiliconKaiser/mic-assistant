#pragma once

#include <cstddef>
#include <cstdint>

namespace amp {

// Play 16 kHz mono samples through the MAX98357A on I2S port 1. Applies `gain`, a peak limit
// for the 1 W speaker, and 20 ms fades. Blocks until done. Plays nothing and returns false if
// the amp has no power (VSW off: on USB with the latch off).
bool play(const int16_t *samples, size_t n, float gain);

}  // namespace amp
