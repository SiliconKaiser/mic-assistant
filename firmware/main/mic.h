#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace mic {

constexpr int SAMPLE_RATE = 16000;  // 16-bit mono

// Start the PDM mic and a capture task that records into a PSRAM buffer.
// `notify` gets a task notification when the first audio arrives.
bool start(TaskHandle_t notify);

// Stop capturing. The recording stays readable through samples().
void stop();

const int16_t *samples();
size_t sample_count();
size_t capacity();  // recording stops by itself once this many samples are captured

// esp_timer time of the first audio (us since app start, not reset), or 0 before it arrives.
int64_t first_audio_us();

}  // namespace mic
