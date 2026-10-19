// Breadboard prototype: XIAO ESP32-S3, Pololu #2808 latch, see hardware/prototype/netlist.md
#pragma once

#define BOARD_NAME "prototype"

// Power
#define PIN_LATCH_OFF      4   // D3 -> Pololu OFF (high pulse = power off)
#define LATCH_SELF_HOLDING 1   // Pololu stays on by itself; firmware only pulses OFF
#define PIN_PTT            2   // D1, via 1N4148; low while pressed (internal pull-up)

// Battery sense: VSW -> 220k -> GPIO1 -> 100k -> GND
#define PIN_VBAT_SENSE     1   // D0, ADC1 channel 0
#define VBAT_DIVIDER_NUM   320 // (220k + 100k)
#define VBAT_DIVIDER_DEN   100 //  100k

// Audio out: MAX98357A (I2S port 1; port 0 is reserved for PDM RX)
#define PIN_I2S_BCLK       5   // D4
#define PIN_I2S_LRCLK      6   // D5
#define PIN_I2S_DOUT       43  // D6
#define PIN_AMP_SD         7   // D8, low = amp shut down

// Audio in: Adafruit PDM mic (I2S port 0)
#define PIN_MIC_CLK        8   // D9
#define PIN_MIC_DAT        9   // D10

// Status LED: NeoPixel
#define PIN_LED_DIN        44  // D7
#define PIN_USER_LED       21  // XIAO onboard LED, active low
