# PCB netlist (v1 draft)

Status: design draft, 2026-10-24. Not yet drawn in KiCad. The goal is that the schematic can be entered from this file pin by pin. Part numbers and stock come from the JLCPCB catalog, checked the same day. BOMs: [bom/](bom/).

One board, components on the top side only (cheaper JLCPCB assembly). It mounts under the top of the case with its components facing up, so the PTT switch, LED and mic sit directly under the button and the mic port. The speaker and battery holder connect by cable.

What changes from the [prototype](../prototype/netlist.md):

- **The latch is held on by the MCU.** Pressing S3 turns Q2 on. Firmware drives **PWR_HOLD** (GPIO4) high as early as possible in boot, keeping Q2 on after the button is released. Driving it low, or a reset, cuts power. This is the `LATCH_SELF_HOLDING = false` mode that `power.cpp` already supports. Measured boot time is ~55 ms to app start, and push-to-talk presses are always longer than that.
- **Reverse-polarity protection** (Q1) in front of the latch. The Pololu board had this built in.
- **The 3.3 V regulator is on the board** (U3). It runs from the 5 V rail or USB, whichever is higher, like the XIAO's.
- **The LED runs at 3.3 V, within its rating.** The WS2812B-2020-V6 is rated 3.3–5.3 V, so it runs on 3V3 the way the prototype's NeoPixel does.
- **Every GPIO is the same as on the prototype.**

## Power path

```
J1 (4×AA) ─VBAT_IN─ Q1 ─VBAT─ Q2 ─VSW─┬─ U1 TPS63070 ─5V─┬─ R13 ─5V_AMP─ U5 amp
            reverse     power        │   buck-boost     └─ D4 ─┐
            protect     switch       └─ R4/R5 → GPIO1          ├─VLDO─ U3 LDO ─3V3_LDO─ R11 ─3V3─ U4, U6, U7
                                                 J2 VBUS ─ D3 ─┘
S3 (PTT) ─BTN─┬─ D1 → Q2 gate (press turns power on)
              └─ D2 → GPIO2 (PTT sense)
GPIO4 ─PWR_HOLD─ Q3 → Q2 gate (holds power on)
```

## Nets

Pin references are `Ref.pin` with the pin's datasheet name. SOT-23 MOSFETs: 1 = G, 2 = S, 3 = D.

| Net | Connects | Notes |
| --- | --- | --- |
| GND | J1.2, J2 GND (A1B12, B1A12) and shell (EH 1–4), Q3.2 (S), R1, R3, R5, R8, R9, R10, R14, C1–C19 (−), U1.4 GND, U1.10 PGND, U1.15 VSEL, U2.2, U3.2, U4.1, U4.40, U4.41 (EPAD), U5.3, U5.11, U5.15, U5.17 (EP), U6.2 LR, U6.5, U7.2, SW1, SW2, SW3 | One ground plane. U6 LR to GND puts the mic on the left slot, as on the prototype. U1 VSEL is tied low because the second output voltage isn't used. |
| VBAT_IN | J1.1, Q1.3 (D) | Battery + before protection. 4.0–6.8 V. |
| VBAT | Q1.2 (S), Q2.2 (S), R2 | Protected battery. If the cells go in backwards, Q1's gate sits above its source, so it stays off and its body diode is reverse-biased. |
| Q1_G | Q1.1 (G), R1 (100k to GND) | Q1 turns fully on once the battery is in the right way round (Vgs = −VBAT). |
| Q2_G | Q2.1 (G), R2 (100k to VBAT), Q3.3 (D), D1 anode | Pulled up to VBAT means off. Pulled low by S3 (through D1) or by Q3 means on. |
| VSW | Q2.3 (D), U1.12, U1.13 VIN, R4, R6, C2, C3, C4 | Battery after the switch. Zero when off. |
| EN_PS | R6 (10k from VSW), U1.14 EN, U1.1 PS/SYNC | TI recommends a series resistor (1k–1M, shareable) when EN and PS/SYNC are tied to the input. PS/SYNC high selects power-save mode at light load. |
| BTN | SW3, D1 cathode, D2 cathode | Low while pressed. Floats when released. |
| PTT | D2 anode, U4.38 IO2 | Internal pull-up on. Reads low while pressed, ~0.5 V through D2, as on the prototype. |
| PWR_HOLD | U4.4 IO4, Q3.1 (G), R3 (100k to GND) | High keeps power on. R3 keeps Q3 off while U4 is unpowered or resetting. |
| VBAT_SENSE | R4 (220k from VSW), R5 (100k to GND), C1 (100 nF), U4.39 IO1 | Same divider as the prototype: firmware ratio 320/100. ADC1 channel 0. |
| L1A | U1.11 L1, L1 pin 1 | Keep this loop short, per the datasheet layout. |
| L1B | U1.9 L2, L1 pin 2 | |
| VAUX | U1.3, C5 (100 nF) | Internal regulator cap. Don't load it. |
| 5V | U1.7, U1.8 VOUT, C6, C7, C8, C9, R7, D4 anode, R13 | Buck-boost output, 5.09 V. Datasheet: 3×22 µF plus one small cap close to the pins. |
| FB | U1.5 FB, R7 (300k to 5V), R8 (56k to GND) | 0.8 V × (1 + 300/56) = 5.09 V. Both values are JLCPCB basic parts. The datasheet example, 680k/130k, would need two extended parts. |
| 5V_AMP | R13 (0 Ω), U5.7, U5.8 VDD, C15, C16, C17 | R13 is a jumper: lift it to measure amp current or isolate the amp. |
| VBUS | J2 VBUS (A4B9, B4A9), U2.5, D3 anode | USB power. Powers the MCU only. The amp still needs the battery path on, as on the prototype. |
| VLDO | D3 cathode, D4 cathode, U3.1 VIN, U3.3 EN, C10 | The higher of USB and 5V, minus a Schottky drop (~4.6 V). The diodes keep USB from back-feeding the 5 V rail and the battery. |
| 3V3_LDO | U3.5 VOUT, C11, R11 | |
| 3V3 | R11 (0 Ω), U4.2, C12, C13, R12, U6.1 Vdd, C18, U7.4 VDD, C19 | Lift R11 to measure MCU current. |
| CC1 | J2.A5, R9 (5.1k to GND) | Identifies the board as a USB sink, so a USB-C charger supplies 5 V. |
| CC2 | J2.B5, R10 (5.1k to GND) | |
| USB_DP | J2.A6, J2.B6, U2.3, U2.4, U4.14 IO20 | Route as a 90 Ω differential pair, short, no stubs. |
| USB_DN | J2.A7, J2.B7, U2.1, U2.6, U4.13 IO19 | |
| EN | U4.3, R12 (10k to 3V3), C14 (1 µF), SW1 | Espressif's reset delay. SW1 = reset. |
| BOOT | U4.27 IO0, SW2 | Hold SW2 while resetting to enter the bootloader. Only needed if USB flashing breaks. IO0 has an internal pull-up. |
| I2S_BCLK | U4.5 IO5, U5.16 BCLK | |
| I2S_LRCLK | U4.6 IO6, U5.14 LRCLK | |
| I2S_DOUT | U4.37 TXD0 (IO43), U5.1 DIN | Same pin as the prototype, so the ROM boot banner still goes to the amp's input. Firmware already holds the amp pins until VSW is present. |
| AMP_SD | U4.7 IO7, U5.4 SD_MODE, R14 (100k to GND) | Low = amp off. GPIO high (3.3 V) selects the left channel. Firmware sends both slots. |
| SPK+ | U5.9 OUTP, J3.1 | Speaker only. Never connect either side to ground. Keep these traces away from the antenna and the mic. |
| SPK− | U5.10 OUTN, J3.2 | |
| MIC_CLK | U4.12 IO8, U6.3 CLK | |
| MIC_DAT | U4.17 IO9, U6.4 DOUT | |
| LED_DIN | U4.36 RXD0 (IO44), U7.3 DI | |

## Not connected

- U1.2 PG, U1.6 FB2.
- U2: nothing else.
- J2 SBU1 (A8), SBU2 (B8).
- U4: every GPIO not listed above. Never connect IO35, IO36 and IO37: the module's octal PSRAM uses them. IO3, IO45 and IO46 are strapping pins and are left floating. IO45 must stay low at boot for the module's 3.3 V flash, and its internal pull-down handles that.
- U5.2 GAIN_SLOT (open = 9 dB, as on the prototype), U5.5, U5.6, U5.12, U5.13.
- U7.1 DO.

## Parts

| Ref | Part | Value / MPN | LCSC |
| --- | --- | --- | --- |
| U1 | Buck-boost, 2–16 V in | TI TPS63070RNMR | C109322 |
| U2 | USB ESD protection | USBLC6-2SC6 (UMW) | C2687116 |
| U3 | 3.3 V LDO, 600 mA | Diodes AP2112K-3.3TRG1 | C51118 |
| U4 | ESP32-S3 module, 8 MB flash, 8 MB octal PSRAM, PCB antenna | Espressif ESP32-S3-WROOM-1-N8R8 | C2913201 |
| U5 | I2S class-D amp | ADI MAX98357AETE+T | C910544 |
| U6 | PDM MEMS mic | ST MP34DT05TR-A | C2688664 |
| U7 | RGB LED, 3.3–5.3 V | Worldsemi WS2812B-2020-V6 | C52917434 |
| Q1, Q2 | P-MOSFET, 30 V, 4 A | AO3401A (AOS) | C15127 |
| Q3 | N-MOSFET | 2N7002 (CJ) | C8545 |
| D1, D2 | Signal diode | 1N4148W | C81598 |
| D3, D4 | Schottky, 1 A 40 V | B5819W | C8598 |
| L1 | 1.5 µH, Isat 4 A, 4×4 mm | cjiang FNR4018S1R5MT | C167802 |
| J1 | Battery, JST PH 2-pin SMD | JST S2B-PH-SM4-TB | C295747 |
| J2 | USB-C receptacle, 16-pin | Hroparts TYPE-C-31-M-12 | C165948 |
| J3 | Speaker, 1.25 mm 2-pin SMD (PicoBlade style) | HCTL HC-1.25-2PWT | C2845379 |
| SW1, SW2 | Reset, boot (5.1 mm tactile) | XKB TS-1187A-B-A-B | C318884 |
| SW3 | PTT, 12×12 mm tactile, round 7 mm actuator | XUNPU TS-1003S-07026 | C455223 |
| R1, R2, R3, R5, R14 | 100 kΩ 1% 0603 | | C25803 |
| R4 | 220 kΩ 1% 0603 | | C22961 |
| R6, R12 | 10 kΩ 1% 0603 | | C25804 |
| R7 | 300 kΩ 1% 0603 | | C23024 |
| R8 | 56 kΩ 1% 0603 | | C23206 |
| R9, R10 | 5.1 kΩ 1% 0603 | | C23186 |
| R11, R13 | 0 Ω 0805 (jumper) | | C17477 |
| C1, C5, C13, C17, C18, C19 | 100 nF 50 V X7R 0603 | | C14663 |
| C14 | 1 µF 50 V X5R 0603 | | C15849 |
| C2, C3, C10, C11, C16 | 10 µF 25 V X5R 0805 | | C15850 |
| C4, C9 | 10 µF 25 V X5R 0603 (next to U1's pins) | | C96446 |
| C6, C7, C8, C12, C15 | 22 µF 25 V X5R 0805 | | C45783 |

Off the board: LS1 speaker (4 Ω 3 W, plug fits J3), BT1 4×AA holder (plug fits J1). See [bom/README.md](bom/README.md).

## Layout notes

- **U4 antenna:** put the module at a board edge with its antenna end overhanging, or keep a copper-free area under the antenna on every layer, as in Espressif's guidelines. Keep the speaker magnet and the cells away from it.
- **U1:** copy the datasheet's layout (§11): input caps, inductor and output caps tight around the chip, on the same layer as it, with a solid ground underneath.
- **U5:** solder the exposed pad to ground with vias. Put C17 and C16 right at the VDD pins.
- **U6:** it's a top-port mic. Confirm that in the datasheet before layout. It faces the case's mic hole, sealed with a foam gasket. Keep it away from L1, the speaker traces and the speaker itself.
- **Test pads:** VBAT, VSW, 5V, 5V_AMP, VLDO, 3V3, GND, BTN, PWR_HOLD and VBAT_SENSE.

## Firmware changes for `boards/pcb.h`

- Copy `prototype.h` and set `LATCH_SELF_HOLDING = false`. Keep `PIN_LATCH_OFF = GPIO_NUM_4`: `power.cpp` uses the same pin for both latch types. `power::init()` drives it high, and `power::off()` drives it low.
- The divider is unchanged (320/100). Calibrate it once per board.
- There's no onboard user LED (the XIAO's was GPIO21): drop `PIN_USER_LED` or leave it unused.
- `sdkconfig`: 8 MB flash and octal PSRAM, the same as the XIAO.
