# PTT Voice Satellite — Prototype Parts List

As of 2026-10-17.

This is everything needed to breadboard the push-to-talk voice satellite. The project-specific parts come to about $50; the rest are common parts and tools you probably already own.

- **Likely have** is a guess at what a hobbyist already owns. Skim those rows and buy only what's missing.
- **Have** is a checkbox for tracking: change `[ ]` to `[x]`.
- Prices are approximate USD. Only the Pololu switch price was checked on the vendor's page.
- Between uses the device is fully off: a latch disconnects the battery from everything after it.



## Power and signal path

```
 4×AA ──▶ Pololu latch ──▶ 5 V regulator ──┬──▶ MAX98357A amp ──▶ Speaker
 4.0–6.4 V  0.01 µA off     + 1000 µF cap   │         ▲
              ▲      ▲                      │         │ I2S + enable (GPIO5, 6, 43, 7)
   press:     │      │ OFF pulse            ▼         │
   latch on   │      │ (GPIO4)        1N5817 Schottky │
              │      │                      │         │
 PTT button ──┘      └──────────── XIAO ESP32-S3 Sense ──▶ Status LED (GPIO44)
 (to GND)  ──── 1N4148 ──▶ GPIO2    5V pin → onboard 3.3 V regulator
                                    built-in PDM mic (GPIO41/42)
                                    GPIO1 ◀── battery divider (200k/100k)
```

Pressing the button turns the latch on. The firmware then holds power until the reply finishes, and pulses OFF to cut everything.

## Project-specific parts

These seven rows are the ones you'll almost certainly need to order.


| Have | Item                                                                                    | Qty         | Spec and role                                                                                                                                                 | Approx. USD | Likely have |
| ---- | --------------------------------------------------------------------------------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- | ----------- |
| [ ]  | [Seeed XIAO ESP32-S3 Sense](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | 1           | Brain and mic. 8 MB PSRAM, 8 MB flash, built-in PDM mic (GPIO41/42), 2.4 GHz Wi-Fi. Ships with an external antenna.                                           | 14          | No          |
| [ ]  | Adafruit MAX98357A I2S amp breakout (#3006)                                             | 1           | Speaker amp. Runs on 2.5–5.5 V, so it goes on the 5 V rail. Screw terminal for the speaker; SD pin shuts it off.                                              | 6           | No          |
| [ ]  | Speaker, 8 Ω 1 W: Adafruit enclosed #5986 (or mini oval #3923)                          | 1           | 8 Ω draws about half the peak current of 4 Ω. The enclosed one sounds better without a case. Cut off the connector and use the amp's screw terminal.          | 4           | No          |
| [ ]  | [Pololu Mini Pushbutton Power Switch LV (#2808)](https://www.pololu.com/product/2808)   | 1           | The power latch. 2.2–16 V, about 0.01 µA when off. A high pulse on OFF cuts power.                                                                            | 4.95        | No          |
| [ ]  | Pololu S9V11F5 5 V buck-boost regulator (#2836)                                         | 1           | Stable 5 V from the falling battery voltage. 2–16 V in (needs 3 V to start), about 1.5 A out. No reverse-polarity protection; the latch in front provides it. | 12          | No          |
| [ ]  | 4×AA battery holder with leads                                                          | 1           | Four cells instead of three: less current per cell and less sag at Wi-Fi peaks. Skip models with a built-in switch, or leave the switch on.                   | 3           | Maybe       |
| [ ]  | AA alkaline cells                                                                       | 4 + 4 spare | Spares for testing with partly drained cells.                                                                                                                 | 5           | Yes         |




## Passives and small parts

Most of these come in standard assortment kits. The bulk capacitor and the 1N5817 are the ones most often missing.


| Have | Item                                                                                      | Qty    | Spec and role                                                                                                                             | Approx. USD | Likely have |
| ---- | ----------------------------------------------------------------------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------- | ----------- | ----------- |
| [ ]  | Electrolytic capacitor, 1000 µF, 10 V or higher, low-ESR                                  | 1      | Bulk cap on the 5 V rail, close to the regulator. Absorbs Wi-Fi and speaker current spikes that cause brownouts.                          | 1           | Maybe       |
| [ ]  | Electrolytic capacitor, 100 µF                                                            | 1      | At the amp's VIN.                                                                                                                         | 0.30        | Yes         |
| [ ]  | Ceramic capacitor, 10 µF                                                                  | 2      | One at the amp's VIN, one at the XIAO's 5V pin.                                                                                           | 0.30        | Yes         |
| [ ]  | Ceramic capacitor, 0.1 µF                                                                 | 3      | XIAO 5V pin, battery divider, spare.                                                                                                      | 0.10        | Yes         |
| [ ]  | Schottky diode, 1N5817 (or SS14)                                                          | 1      | Between the regulator and the XIAO's 5V pin, as the Seeed wiki requires. Also stops USB from back-feeding the regulator.                  | 0.20        | Maybe       |
| [ ]  | Signal diode, 1N4148                                                                      | 1      | Lets a GPIO read the PTT button without battery voltage reaching it. Chosen for its nanoamp leakage while the device is off.              | 0.05        | Yes         |
| [ ]  | Resistors, 200 kΩ and 100 kΩ                                                              | 1 each | Battery divider to the ADC: 6.4 V from fresh cells reads as about 2.1 V. It sits after the latch, so it draws nothing when off.           | <0.10       | Yes         |
| [ ]  | Resistor, 100 kΩ                                                                          | 1      | Pull-down on the latch's OFF pin so it can't trigger during boot. Optional safety.                                                        | <0.10       | Yes         |
| [ ]  | Resistor, 10 kΩ                                                                           | 1      | Pull-down on the amp's SD pin. Keeps the amp off, with no pop, until the firmware turns it on.                                            | <0.10       | Yes         |
| [ ]  | Momentary pushbutton, low bounce                                                          | 1      | The PTT button: 12 mm tactile or a panel-mount one. Pololu warns that buttons bouncing for several ms may not trigger the latch reliably. | 1–3         | Maybe       |
| [ ]  | Status LED: one WS2812/SK6812 (NeoPixel) breakout, or a common-cathode RGB LED + 3× 330 Ω | 1      | Listening, thinking and error states. For a first test, the XIAO's built-in LED (GPIO21) is enough.                                       | 1–3         | Maybe       |




## Prototyping and wiring supplies

All common shop stock. The one thing to watch is wire gauge: the battery-to-regulator-to-amp path carries up to about 0.8 A peaks.


| Have | Item                                          | Qty        | Spec and role                                                                                                                             | Approx. USD | Likely have |
| ---- | --------------------------------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------- | ----------- | ----------- |
| [ ]  | Full-size breadboard                          | 1          | First build. Fine for signals, but its contacts sag under high current, so move the power path to wire or perfboard if you see brownouts. | 5           | Yes         |
| [ ]  | Jumper wires, short (male-male)               | 1 kit      | Keep I2S and mic wires short; long jumpers add noise.                                                                                     | 5           | Yes         |
| [ ]  | Male header pins, 0.1 in                      | 2 strips   | The XIAO and the Pololu boards need headers soldered on. Pololu includes them; check the XIAO package.                                    | 1           | Yes         |
| [ ]  | Hookup wire, 22–24 AWG (silicone or stranded) | 1 roll     | Battery, latch, regulator and amp power path.                                                                                             | 5           | Yes         |
| [ ]  | Perfboard or solderable protoboard            | 1          | Second build, once the breadboard version works.                                                                                          | 4           | Maybe       |
| [ ]  | Heat-shrink tubing                            | assortment | Insulating battery and speaker leads.                                                                                                     | 5           | Yes         |
| [ ]  | USB-C data cable                              | 1          | Flashing and serial logs. Must carry data, not just charge.                                                                               | 5           | Yes         |
| [ ]  | Double-sided foam tape, zip ties              | some       | Holding the speaker, battery pack and boards in place.                                                                                    | 3           | Yes         |
| [ ]  | JST-PH 2-pin pigtails                         | 2 pairs    | Optional: plug-in battery and speaker leads instead of hard-wiring.                                                                       | 3           | Maybe       |




## Tools

Only the first four are required. The optional ones are how you verify the two hard targets: under 300 ms to listening, and near-zero current when off.


| Have | Tool                                          | Required?             | Why                                                                                   | Approx. USD | Likely have |
| ---- | --------------------------------------------- | --------------------- | ------------------------------------------------------------------------------------- | ----------- | ----------- |
| [ ]  | Soldering iron, solder, flux                  | Required              | Headers, diodes and perfboard.                                                        | 30+         | Yes         |
| [ ]  | Multimeter with a µA range                    | Required              | Check that off current is near 0 and check rail voltages.                             | 25+         | Yes         |
| [ ]  | Wire strippers, flush cutters                 | Required              |                                                                                       | 10+         | Yes         |
| [ ]  | Computer with ESP-IDF (or Arduino/PlatformIO) | Required              | Flashing firmware. ESP-IDF gives the boot-time controls needed for the 300 ms target. | 0           | Yes         |
| [ ]  | Nordic PPK2 power profiler                    | Optional, very useful | Shows current spikes, voltage sag and press-to-awake time in one trace.               | 100         | No          |
| [ ]  | Logic analyzer, 8-channel                     | Optional              | Times press, then power good, then firmware running, then first audio frame.          | 10–15       | Maybe       |
| [ ]  | Bench power supply with current limit         | Optional              | Feed in 4.0 V or 3.2 V to imitate drained cells and find where it browns out.         | 50+         | Maybe       |
| [ ]  | USB power meter                               | Optional              | Quick current readings while powered over USB.                                        | 15          | Maybe       |
| [ ]  | Helping hands or a PCB holder                 | Optional              | Easier soldering of small boards.                                                     | 10          | Yes         |




## Optional upgrades and spares

None of these are needed for the first working prototype. Each one solves a specific problem if it comes up.


| Have | Item                                                                     | Qty | When you'd want it                                                                                                                                                                              | Approx. USD | Likely have |
| ---- | ------------------------------------------------------------------------ | --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- | ----------- |
| [ ]  | External I2S mic: Adafruit SPH0645 breakout, or a generic INMP441 module | 1   | The built-in mic is too weak from across a room. Adafruit discontinued the ICS-43434 and names the SPH0645 as its replacement; the SPH0645 needs a known I2S timing workaround on the ESP32-S3. | 3–7         | No          |
| [ ]  | Lithium AA cells (Energizer L91)                                         | 4   | Diagnosing sag: if a brownout disappears on lithium cells, alkaline internal resistance is the cause.                                                                                           | 10          | No          |
| [ ]  | TI TPL5110 nano-power timer breakout                                     | 1   | Scheduled wakes (battery report, update check) while staying fully off in between, at about 35 nA.                                                                                              | 5           | No          |
| [ ]  | Plain XIAO ESP32-S3 (non-Sense)                                          | 1   | If you move to an external mic: smaller, and frees the camera connector's pins.                                                                                                                 | 8           | No          |
| [ ]  | Spare Pololu #2808 latch                                                 | 1   | It's cheap, and a wiring mistake can kill one.                                                                                                                                                  | 4.95        | No          |
| [ ]  | Small project box, or a 3D-printed case                                  | 1   | Speakers sound much better with a sealed back volume. The mic needs a port hole; the LED needs a light pipe or diffuser.                                                                        | 5–10        | Maybe       |




## Pin map and wiring notes

This pin map avoids the strapping pins (GPIO0, 3, 45, 46), the camera pins, and the LED/SD-card overlap on GPIO21. Check it against the Seeed pin-multiplexing page before soldering.


| Function         | XIAO pin | GPIO           | Connects to                                                      |
| ---------------- | -------- | -------------- | ---------------------------------------------------------------- |
| Battery ADC      | D0       | GPIO1          | Midpoint of the 200 kΩ / 100 kΩ divider, 0.1 µF to GND           |
| PTT button sense | D1       | GPIO2          | 1N4148 anode (internal pull-up on)                               |
| Latch OFF        | D3       | GPIO4          | Pololu OFF pin, 100 kΩ to GND                                    |
| I2S BCLK         | D4       | GPIO5          | Amp BCLK                                                         |
| I2S LRCLK        | D5       | GPIO6          | Amp LRC                                                          |
| I2S data out     | D6       | GPIO43         | Amp DIN                                                          |
| LED data         | D7       | GPIO44         | NeoPixel DIN                                                     |
| Amp enable       | D8       | GPIO7          | Amp SD, 10 kΩ to GND. Free only while the microSD slot is empty. |
| Mic              | built in | GPIO41, GPIO42 | Onboard PDM mic (data, clock)                                    |


- **Button:** one leg to GND, the other to the latch's A pin. The 1N4148 runs from the GPIO (anode) to that same node (cathode). A press pulls both low. When released, the diode blocks the battery voltage on A from reaching the GPIO.
- **Power-off:** firmware waits for the button release, then pulses OFF high. Pololu doesn't document a minimum pulse width, so measure it.
- **USB:** while USB is plugged in, the board stays powered no matter what the latch does. Test power-off and off current on battery only.
- **Power path:** keep the 1000 µF capacitor at the regulator's output and use heavier wire from the battery through the latch and regulator to the amp.
- **Amp:** hold SD low until the I2S clock is running, then drive it high. This avoids pops at power-on and between turns.
- **Antenna:** attach it before the first Wi-Fi test.
- **Boot speed:** set log levels to none or error, turn on `BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON`, use QIO flash mode, and turn off the PSRAM memory test (about 1 s per 4 MB).



## Sources

Pages opened and read:

- [Pololu Mini Pushbutton Power Switch LV #2808](https://www.pololu.com/product/2808): pins, off current, price
- [Seeed XIAO ESP32-S3 getting started](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/): power inputs, deep sleep figures, mic and strapping pins
- [ESP-IDF speed and startup-time guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/performance/speed.html): boot-time settings
- [Adafruit ESP-NOW Walkie Talkies](https://learn.adafruit.com/esp-now-walkie-talkies.md?view=all): a similar ESP32-S3 + MAX98357A + I2S mic + PTT build

From search results (summaries, not read page by page):

- [Pololu S9V11F5 specs](https://www.pololu.com/product/2836/specs)
- [Seeed XIAO ESP32-S3 pin multiplexing](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/)
- [Adafruit ICS-43434 breakout (discontinued)](https://adafruit.com/product/6049)
- [SPH0645 with ESP32-S3 workaround](https://esp32.com/viewtopic.php?p=127222)
- [ESP32 brownout on 2×AA plus boost](https://esp32.com/viewtopic.php?p=14803)
- [SMPS brownout fixed with a larger output capacitor](https://esp32.com/viewtopic.php?p=42934)
- [ESP32-S3 power consumption table](https://deneyapkart.org/en/docs/guc-tuketimi-esp32-s3)
- [ESP32-S3 hardware design guidelines](https://documentation.espressif.com/esp-hardware-design-guidelines/en/latest/esp32s3/index.html)
- [m5atom walkie-talkie (INMP441 + MAX98357)](https://github.com/febalci/m5atom-walkie-talkie)

