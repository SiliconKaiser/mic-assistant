#pragma once

#include <cstddef>
#include <cstdint>

namespace dump {

// Debug: send 16-bit mono samples over the USB console as a text block that
// firmware/tools/monitor.py turns into a .wav file. Does nothing unless a USB host is connected.
// Block format, one item per line:
//   WAV <sample_rate> <sample_count> <crc32 of the raw little-endian bytes, hex>
//   <base64 lines, 64 chars each>
//   WAV END
void wav(const int16_t *samples, size_t n, int sample_rate);

}  // namespace dump
