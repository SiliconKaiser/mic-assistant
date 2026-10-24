# Prototype netlist

Breadboard build: plain XIAO ESP32-S3, Adafruit PDM mic, MAX98357A amp, NeoPixel, Pololu latch and 5 V regulator, 4×AA holder.

## Nets

| Net | Connects | Notes |
| --- | --- | --- |
| VBAT | BT1 +, U1 VIN | Battery before the latch. 4.0–6.8 V (brand-new alkalines read ~1.7 V each). |
| GND | BT1 −, U1 GND, U2 GND, U3 GND, U4 GND, U5 GND, U5 SEL, U6 GND, S1, C1, C2, C3, C4, C5, C6, R2, R3, R4 | One common ground. |
| VSW | U1 VOUT, U2 VIN, R1 | Battery voltage after the latch. Zero when off. |
| 5V | U2 VOUT, C1 +, U4 VIN, C5 +, C6, D1 anode | Regulator output. |
| XIAO_5V | D1 cathode, U3 5V, C2, C3 | Keeps USB 5 V from back-feeding the regulator. |
| 3V3 | U3 3V3, U5 3V, U6 VCC | XIAO's onboard regulator output. |
| BTN | S1, U1 A, D2 cathode | Press pulls A low and turns the latch on. |
| PTT | D2 anode, U3 D1 (GPIO2) | Internal pull-up on. Reads low while pressed. |
| LATCH_OFF | U3 D3 (GPIO4), U1 OFF, R3 | High pulse cuts power. R3 keeps it low during boot. |
| VBAT_SENSE | R1/R2 midpoint, C4, U3 D0 (GPIO1) | 6.8 V reads about 2.1 V. Use ADC attenuation 12 dB. |
| I2S_BCLK | U3 D4 (GPIO5), U4 BCLK | |
| I2S_LRCLK | U3 D5 (GPIO6), U4 LRC | |
| I2S_DOUT | U3 D6 (GPIO43), U4 DIN | GPIO43 is UART TX: the ROM boot banner goes to the amp while it's shut down. On battery that's harmless. On USB with the latch off the amp is unpowered, and driving its inputs back-powers it, so firmware leaves the amp pins floating until VSW is present. |
| AMP_SD | U3 D8 (GPIO7), U4 SD, R4 | Low = amp off. Drive high only once I2S is running, to avoid pops. |
| LED_DIN | U3 D7 (GPIO44), U6 DIN | |
| MIC_CLK | U3 D9 (GPIO8), U5 CLK | |
| MIC_DAT | U3 D10 (GPIO9), U5 DAT | SEL to GND puts the mic on the left channel. |
| SPK± | U4 SPK+, U4 SPK− to LS1 | Speaker only. Never connect either side to ground. |

Not connected: U1 ON/CTRL/B, U2 EN (pulled up on the board), U3 D2 (GPIO3, a strapping pin), U4 GAIN (default 9 dB), U6 DOUT.

## Parts

| Ref | Part | Source |
| --- | --- | --- |
| BT1 | 4×AA holder (bench-tested); a 4×AAA clip also works | Owned / Mouser 485-830 |
| U1 | Pololu Mini Pushbutton Power Switch LV #2808 | Pololu |
| U2 | Pololu S9V11F5 5 V buck-boost #2836 | Pololu |
| U3 | Seeed XIAO ESP32-S3 | Mouser 713-113991114 |
| U4 | Adafruit MAX98357A #3006 | Mouser 485-3006 |
| U5 | Adafruit PDM mic #3492 | Mouser 485-3492 |
| U6 | Adafruit NeoPixel mini #1612 | Mouser 485-1612 |
| LS1 | 8 Ω 1 W mini oval #3923 (or 4 Ω enclosed #3351) | Mouser |
| S1 | Push button | Owned |
| D1 | 1N5817 | Mouser 863-1N5817G / kit 1N5819 |
| D2 | 1N4148 | Owned kit |
| C1 | 1000 µF 10 V electrolytic | Mouser 647-UHE1A102MPD |
| C2, C6 | 10 µF electrolytic, 10 V or higher | Owned kit |
| C3, C4 | 0.1 µF ceramic (marked 104) | Owned kit |
| C5 | 100 µF electrolytic, 10 V or higher | Owned kit |
| R1 | 220 kΩ | Owned kit |
| R2, R3 | 100 kΩ | Owned kit |
| R4 | 10 kΩ | Owned kit |

## Before the first power-up

1. Check continuity from VBAT to GND with the multimeter. It should read open, not a short.
2. Insert the cells with nothing powered. VSW should read 0 V; U1's LED should be off.
3. Press S1. VSW should match the battery voltage, and 5V should read 5.0 V.
4. Leave USB unplugged during battery tests. While USB is plugged in, the XIAO stays powered no matter what the latch does.
