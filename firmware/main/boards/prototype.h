// Breadboard prototype: XIAO ESP32-S3, Pololu #2808 latch, see hardware/prototype/netlist.md
// Bench-tested stage by stage on 2026-10-24 with hardware/prototype/assembly-guide.md.
#pragma once

#include "driver/gpio.h"

namespace board {

inline constexpr const char *NAME = "prototype";

// --- Power ---
// D3 -> Pololu OFF. A high pulse cuts power. R3 (100k) holds it low from reset until
// power::init() drives it, so it can't float high during boot and cut power.
inline constexpr gpio_num_t PIN_LATCH_OFF = GPIO_NUM_4;
inline constexpr bool LATCH_SELF_HOLDING = true;  // Pololu stays on by itself; firmware only pulses OFF
// D1, via 1N4148 (D2) to latch pin A. Low while pressed (internal pull-up).
inline constexpr gpio_num_t PIN_PTT = GPIO_NUM_2;

// --- Battery sense: VSW -> 220k -> GPIO1 -> 100k -> GND ---
// Must be an ADC1 pin: ADC2 can't be read while Wi-Fi is running.
// Pack: 4x AA alkaline, 4.0-6.8 V (brand-new cells read ~1.7 V each).
// Calibration TODO: on this board the reading was ~4% low (ADC 6.50 V vs meter 6.78 V),
// which fits 5% resistor tolerance. Set NUM from a meter reading of VSW taken at the
// same moment as a logged reading: NUM = 320 * meter / logged (about 334 here).
inline constexpr gpio_num_t PIN_VBAT_SENSE = GPIO_NUM_1;  // D0, ADC1 channel 0
inline constexpr int VBAT_DIVIDER_NUM = 320;              // (220k + 100k)
inline constexpr int VBAT_DIVIDER_DEN = 100;              //  100k

// --- Audio out: MAX98357A on I2S port 1 (port 0 is reserved for PDM RX) ---
// The amp runs from the 5V rail, which has no power while the latch is off, even on USB.
// Don't drive these pins until power::vsw_present() is true (see power.cpp).
// GAIN is unconnected: 9 dB. Speaker (LS1): 4 ohm 3 W enclosed (#3351), chosen on the bench
// 2026-10-24: much fuller than the bare 8 ohm 1 W mini oval, and 16x-gain speech playback ran
// on battery with no resets. Into 4 ohm the amp draws ~2x the current of 8 ohm at the same
// setting, so when raising volume, supply sag (XIAO resets) is the limit to watch, not the
// speaker. Earlier, on the 8 ohm: a 500 Hz sine at 90% of full scale ran clean on battery.
inline constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_5;   // D4
inline constexpr gpio_num_t PIN_I2S_LRCLK = GPIO_NUM_6;  // D5
inline constexpr gpio_num_t PIN_I2S_DOUT = GPIO_NUM_43;  // D6. Also UART0 TX: the ROM prints its boot banner here
inline constexpr gpio_num_t PIN_AMP_SD = GPIO_NUM_7;     // D8, low = amp shut down (R4 pulls it low)

// --- Audio in: Adafruit PDM mic on I2S port 0 (PDM RX only works on port 0) ---
// Levels at 16 kHz/16-bit after mic.cpp's DC high-pass (ESP-IDF, 2026-10-24): silence RMS ~6
// (peaks ~25), normal speech RMS ~60 with peaks ~1400 (of 32767). Arduino saw claps ~14000.
// Speech needs ~16x digital gain (or AGC) before it goes to the server. Every recording
// starts with a ~100 ms start-up burst (peaks 4000-6000) the high-pass doesn't remove: drop
// or fade it before streaming.
inline constexpr gpio_num_t PIN_MIC_CLK = GPIO_NUM_8;  // D9
inline constexpr gpio_num_t PIN_MIC_DAT = GPIO_NUM_9;  // D10

// --- Status LED ---
inline constexpr gpio_num_t PIN_LED_DIN = GPIO_NUM_44;  // D7, NeoPixel on XIAO 3V3
inline constexpr gpio_num_t PIN_USER_LED = GPIO_NUM_21; // XIAO onboard LED, active low

}  // namespace board
