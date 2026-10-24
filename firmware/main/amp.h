#pragma once

#include <cstddef>
#include <cstdint>

// MAX98357A on I2S port 1. Mono 16-bit samples at 8-48 kHz, with `gain`, a peak limit (see
// LIMIT) and 20 ms fades. One stream at a time, from one task.
namespace amp {

// Streaming: begin(), write() as audio arrives, end(). begin() is false (and nothing plays) if
// the amp has no power (VSW off: on USB with the latch off) or the I2S setup fails.
bool begin(uint32_t sample_rate);
// Blocks while the DMA queue is full, so it paces the caller to real time. `last` fades out the
// end of this block (the final block of the stream).
void write(const int16_t *samples, size_t n, float gain, bool last = false);
void end();

// The whole buffer in one call.
bool play(const int16_t *samples, size_t n, float gain, uint32_t sample_rate);

}  // namespace amp
