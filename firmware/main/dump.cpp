#include "dump.h"

#include <cstdio>

#include "driver/usb_serial_jtag.h"

namespace dump {
namespace {

// Same CRC-32 as zlib's crc32(), so the host can check it with the standard library.
uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
        }
    }
    return ~crc;
}

// Encodes up to 48 bytes into one 64-char line (plus padding for a short last line).
void put_base64_line(const uint8_t *p, size_t n)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[66];
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i + 1 < n) {
            v |= uint32_t(p[i + 1]) << 8;
        }
        if (i + 2 < n) {
            v |= p[i + 2];
        }
        line[o++] = ALPHABET[(v >> 18) & 63];
        line[o++] = ALPHABET[(v >> 12) & 63];
        line[o++] = i + 1 < n ? ALPHABET[(v >> 6) & 63] : '=';
        line[o++] = i + 2 < n ? ALPHABET[v & 63] : '=';
    }
    line[o++] = '\n';
    fwrite(line, 1, o, stdout);
}

}  // namespace

void wav(const int16_t *samples, size_t n, int sample_rate)
{
    // Debug only: this check makes FreeRTOS track USB SOF packets on every tick. Drop it (and
    // this module) from the product build.
    if (!usb_serial_jtag_is_connected()) {
        return;
    }
    // ESP32-S3 is little-endian, which is what .wav wants, so the raw bytes go as-is.
    auto bytes = reinterpret_cast<const uint8_t *>(samples);
    size_t len = n * sizeof(int16_t);

    printf("WAV %d %u %08lx\n", sample_rate, unsigned(n), (unsigned long)crc32(bytes, len));
    for (size_t i = 0; i < len; i += 48) {
        put_base64_line(bytes + i, len - i < 48 ? len - i : 48);
    }
    printf("WAV END\n");
    fflush(stdout);
}

}  // namespace dump
