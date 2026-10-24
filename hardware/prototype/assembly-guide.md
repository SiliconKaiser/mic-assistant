# Prototype assembly guide

Build the prototype in six stages, testing each one before adding the next. If something fails, the problem is almost always in the part you just added.

The circuit is in [netlist.md](netlist.md). When this guide says "connect to VSW" or "connect to 5V", that's a net from the netlist.

## Before you start

### Prep work (about 1 hour of soldering)

| Board | What to do |
| --- | --- |
| XIAO ESP32-S3 | Solder the two 7-pin headers, pins pointing down. Snap on the U.FL antenna. |
| Pololu #2808 latch | Solder the included headers. Leave its onboard button alone; it toggles power on *and* off. |
| Pololu S9V11F5 | Solder the included 3-pin straight header. |
| MAX98357A amp | Solder its 7-pin header. Screw the speaker wires into the terminal block. |
| PDM mic | Solder its 5-pin header. |
| NeoPixel mini (one from the 5-pack) | Solder 3 short wires to +, −, and In. It has no header. |
| Battery holder (4×AA, or the 4×AAA clip) | If its leads are stranded, tin them or crimp on pins so they seat in the breadboard. |

Use your flux on every joint. Trim the resistor and capacitor leads to about 1 cm before inserting them.

### Pinouts

Wiring steps name pins, not breadboard holes. Place the modules wherever they fit, as long as every pin lands in its own row and no two pins share a row unless they're meant to connect. The XIAO and the latch straddle the center gap.

Use the power rail for **+ = 5V** and **− = GND**. 3V3 only feeds the mic and the NeoPixel, so wire those straight to the XIAO's **3V3** pin. If the rail is split in the middle, bridge it, and check continuity from end to end with the multimeter.

Pin order is top to bottom, with each board's components facing up.

**XIAO ESP32-S3**, USB-C at the top. Check against your board's silkscreen.

| Left | Right |
| --- | --- |
| D0 / GPIO1 | 5V |
| D1 / GPIO2 | GND |
| D2 / GPIO3 | 3V3 |
| D3 / GPIO4 | D10 / GPIO9 |
| D4 / GPIO5 | D9 / GPIO8 |
| D5 / GPIO6 | D8 / GPIO7 |
| D6 / GPIO43 | D7 / GPIO44 |

**Pololu #2808 latch**, MOSFETs at the top and pushbutton at the bottom. The labels are printed on the underside, mirrored.

| Left | Right |
| --- | --- |
| VIN | VOUT |
| VIN | VOUT |
| GND | GND |
| GND | GND |
| ON | A or B (button) |
| OFF | A or B (button) |
| CTRL | (no pin) |

Paired pins are connected on the board, so wire either one. Pololu doesn't document which button pin is A; Stage 1 shows how to find it.

**Pololu S9V11F5 regulator**: three pins in a row. With the header on the right, they read **VIN, GND, VOUT** from top to bottom; GND has the square pad. **EN** is the separate round pad next to GND. Leave it unconnected.

**MAX98357A amp**: LRC, BCLK, DIN, GAIN, SD, GND, Vin. The speaker terminals are marked − and +.

**PDM mic**: 3V, GND, SEL, CLK, DAT.

### Arduino setup (for the test sketches)

1. Install the Arduino IDE, then add the **esp32 by Espressif** boards package, version 3.x.
2. Select the board **XIAO_ESP32S3**, then set **USB CDC On Boot: Enabled** and **PSRAM: OPI PSRAM**.
3. If an upload fails, hold the XIAO's **B** button while plugging in USB, then try again. After an upload in that mode, press **R** (reset) so the sketch runs.

Without the IDE: the IDE ships `arduino-cli`, which uses the same boards package.

```sh
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" core update-index && "$CLI" core install esp32:esp32
# Each sketch lives in a folder of the same name: stage2/stage2.ino
"$CLI" compile --upload -p /dev/cu.usbmodem1101 --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi stage2
```

USB CDC On Boot is already Enabled by default for this board. Don't add `CDCOnBoot=cdc`: on this board that value means *Disabled*.

These sketches are bench tests only. The real firmware will be ESP-IDF.

---

## Stage 1: power path

Put in **no cells yet**, and leave the XIAO unpowered.

Wiring:
1. Battery clip **red** → latch **VIN**. Battery clip **black** → **GND** rail.
2. Latch **GND** → GND rail.
3. Latch **VOUT** → regulator **VIN**. This connection is the **VSW** net.
4. Regulator **GND** → GND rail. Regulator **VOUT** → **5V** rail. Leave **EN** unconnected.
5. **C1** (1000 µF) from the 5V rail to the GND rail, right next to the regulator. **The stripe (−) side goes to GND.**
6. Button **S1**: one leg to latch **A**, the other leg to the GND rail. Pololu doesn't say which button pin is A, so start with the upper one; the checks below confirm it.

Checks:
- [ ] Battery leads: the meter shows no short between red and black.
- [ ] Insert the 4 cells. **VSW reads 0 V** and the 5V rail reads 0 V.
- [ ] Press S1. VSW jumps to the battery voltage (6.5–6.8 V with brand-new alkalines) and the 5V rail reads **4.9–5.1 V**. The latch's LED lights.
- [ ] Press S1 again. Power stays on, because A to GND can only turn the latch on. If the first press did nothing, or the second press turned it off, S1 is on B: move it to the other button pin.
- [ ] Turn power off with the latch's onboard button. VSW returns to 0 V.
- [ ] **Off current:** remove one cell and put the meter, on its µA range, in its place. It should read **under 1 µA**. A reading in the mA range means the latch isn't off or something is wired wrong. **Remove the meter, or switch it to mA, before pressing S1.** On the µA range it drops several volts once the board draws current, which starves it: in later stages that shows up as a flickering LED and phantom button presses.

If something's wrong:
- **Nothing happens on a press:** S1 may be on B instead of A. Move it to the other button pin. Also check S1 goes to GND, not to the other button pin.
- **The 5V rail is low:** the cells may be weak, or the regulator wiring is swapped.

---

## Stage 2: XIAO on battery, and self power-off

Wiring:
1. **D1** (1N5817): anode (no band) on the 5V rail, cathode (band) to XIAO **5V**.
2. **C2** (10 µF electrolytic; stripe to GND) and **C3** (0.1 µF ceramic) from XIAO 5V to GND.
3. XIAO **GND** → GND rail.
4. Leave XIAO **3V3** unconnected for now. The mic and NeoPixel connect to it later.
5. XIAO **D3/GPIO4** → latch **OFF**. Add **R3** (100 kΩ) from D3/GPIO4 to GND.

Sketch: flash it over USB with the cells removed.

```cpp
// Stage 2: blink, then cut our own power via the latch.
const int PIN_OFF = 4;   // D3 -> latch OFF
const int LED = 21;      // onboard user LED, on when LOW

void setup() {
  pinMode(PIN_OFF, OUTPUT);
  digitalWrite(PIN_OFF, LOW);
  pinMode(LED, OUTPUT);
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED, i % 2);
    delay(250);
  }
  delay(3000);
  digitalWrite(PIN_OFF, HIGH);  // latch turns off; on battery we die here
  delay(100);
  digitalWrite(PIN_OFF, LOW);   // only reached on USB power
}

void loop() {}
```

Checks (unplug USB first):
- [ ] Insert the cells and press S1. The XIAO's LED blinks 3 times, and about 3 s later everything powers off: the latch LED goes out and VSW reads 0 V.
- [ ] Press S1 again. It repeats every time.
- [ ] Off current is still **under 1 µA**.

If something's wrong:
- **It never turns off:** check the GPIO4 → OFF wire.
- **It turns off immediately or flickers:** R3 is missing, or C1 isn't next to the regulator.
- **It resets when Wi-Fi starts later:** add more capacitance at XIAO 5V, and keep the power wires short.

---

## Stage 3: button sense, battery reading, NeoPixel

Wiring:
1. **D2** (1N4148): anode (no band) to XIAO **D1/GPIO2**, cathode (band) to the **latch A / button** node.
2. **R1** (220 kΩ) from **VSW** to R2, then **R2** (100 kΩ) to GND. Their junction is **VBAT_SENSE**. Put **C4** (0.1 µF) from VBAT_SENSE to GND, and connect VBAT_SENSE to XIAO **D0/GPIO1**.
3. NeoPixel: **+** → XIAO **3V3**, **−** → GND, **In** → XIAO **D7/GPIO44**.

Sketch: you can run this one on USB.

```cpp
// Stage 3: show button state on the NeoPixel, print battery voltage.
const int PIN_BAT = 1, PIN_PTT = 2, PIN_OFF = 4, PIN_LED = 44;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_OFF, OUTPUT);
  digitalWrite(PIN_OFF, LOW);
  pinMode(PIN_PTT, INPUT_PULLUP);
  analogSetPinAttenuation(PIN_BAT, ADC_11db);
}

void loop() {
  bool pressed = digitalRead(PIN_PTT) == LOW;
  float vbat = analogReadMilliVolts(PIN_BAT) * 3.2f / 1000.0f;  // (220k + 100k) / 100k
  rgbLedWrite(PIN_LED, pressed ? 0 : 20, pressed ? 20 : 0, 0);
  Serial.printf("PTT=%d  VBAT=%.2f V\n", pressed, vbat);
  delay(200);
}
```

Checks:
- [ ] With cells in and USB plugged in, press S1. The LED goes from red to green while the button is held.
- [ ] On USB alone, VBAT reads about 0 V, because VSW is off. After a press on battery, it reads within about 0.2 V of your meter's VSW reading.
- [ ] Unplug USB. Off current is still **under 1 µA**. This confirms D2 isn't leaking.

If something's wrong:
- **PTT always reads pressed:** D2 is in backwards.
- **Pressing no longer turns the latch on:** the diode's forward drop may be too much for pin A. Try a Schottky (1N5817/1N5819) for D2 instead, or tell me and we'll rework it.

---

## Stage 4: microphone

Wiring: mic **3V** → XIAO **3V3**, **GND** → GND, **SEL** → GND, **CLK** → XIAO **D9/GPIO8**, **DAT** → XIAO **D10/GPIO9**. Keep these two jumpers short.

```cpp
// Stage 4: print the mic's peak level. Talk, clap, stay quiet.
#include <ESP_I2S.h>
I2SClass mic;

void setup() {
  Serial.begin(115200);
  mic.setPinsPdmRx(8, 9);  // CLK, DATA
  if (!mic.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO))
    Serial.println("mic init failed");
}

void loop() {
  int16_t buf[512];
  size_t n = mic.readBytes((char *)buf, sizeof(buf)) / 2;
  long peak = 0;
  for (size_t i = 0; i < n; i++) peak = max(peak, (long)abs(buf[i]));
  Serial.println(peak);  // view it in Tools > Serial Plotter
}
```

Checks:
- [ ] Silence gives a low, steady number. Talking at arm's length raises it clearly. A clap spikes it.
- [ ] A reading stuck at 0 or 32767 means CLK and DAT are swapped, or SEL is floating.

---

## Stage 5: amplifier and speaker

Wiring:
1. Amp **Vin** → 5V rail. Amp **GND** → GND.
2. **C5** (100 µF) and **C6** (10 µF) from amp Vin to GND, next to the amp. Both are electrolytic: stripe to GND.
3. Amp **BCLK** → XIAO **D4/GPIO5**. Amp **LRC** → **D5/GPIO6**. Amp **DIN** → **D6/GPIO43**.
4. Amp **SD** → XIAO **D8/GPIO7**, with **R4** (10 kΩ) from SD to GND.
5. Leave **GAIN** unconnected for 9 dB.
6. Speaker wires go only in the amp's terminal block. **Never connect either speaker wire to GND.**

The amp runs from the 5V rail, so **the latch must be on** (cells in, press S1) even while you're on USB. With the latch off, the amp only gets power leaking in through its signal pins, and it crackles.

```cpp
// Stage 5: play a 500 Hz tone, 1 s on / 1 s off.
#include <ESP_I2S.h>
I2SClass amp;
const int PIN_SD = 7;
int16_t tone500[160];  // 10 ms at 16 kHz = exactly 5 cycles of 500 Hz

void setup() {
  pinMode(PIN_SD, OUTPUT);
  digitalWrite(PIN_SD, LOW);  // amp off until I2S runs
  for (int i = 0; i < 160; i++) tone500[i] = (int16_t)(4000 * sinf(2 * PI * 500 * i / 16000.0f));
  amp.setPins(5, 6, 43);  // BCLK, LRCLK, DOUT
  amp.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
}

void loop() {
  digitalWrite(PIN_SD, HIGH);
  for (int i = 0; i < 100; i++) amp.write((uint8_t *)tone500, sizeof(tone500));
  digitalWrite(PIN_SD, LOW);
  delay(1000);
}
```

Checks:
- [ ] A clean tone, 1 s on and 1 s off. A pop when SD switches is normal in this test; the real firmware will ramp the audio.
- [ ] Test on battery too. Upload a version that powers off afterward, or just watch: the tone shouldn't make the XIAO reset. If it does, the 5V rail is sagging. Check C1, C5, and the power wiring.
- [ ] Too loud or distorted: lower the 4000 amplitude.

---

## Stage 6: everything together

Once all five stages pass:

- [ ] All parts connected, on battery, USB unplugged. A press boots the XIAO, and the Stage 2 sketch still powers off cleanly.
- [ ] Off current with everything connected is still **under 1 µA**.
- [ ] Measure press-to-boot time. Have the sketch's first line turn the NeoPixel on, then film the button and LED in slow motion on your phone. That's the starting point for the 300 ms goal; boot-time tuning comes in the ESP-IDF firmware.

The full push-to-talk test (talk → server → reply) needs the real firmware and the home server, which come next.

### Note for the firmware

On the ESP32-S3, the PDM mic only works on I2S port 0. When the mic and amp run together, the mic has to claim port 0 and the amp has to use port 1. The stage sketches each use only one, so they don't hit this.

## Common problems

| Symptom | Likely cause |
| --- | --- |
| Resets when Wi-Fi starts or audio plays | 5V sag: C1 not next to the regulator, long or thin power wires, weak cells |
| Won't power off on battery | GPIO4 → OFF wire, or the sketch isn't pulsing OFF |
| Stays on with USB unplugged | Check that USB really is unplugged. Then make sure nothing connects 5V or 3V3 back to VSW |
| Off current in mA | The latch is still on, or something is connected to VBAT before the latch |
| Hum or noise in the mic | Mic jumpers too long, or the mic's ground shares a long path with the amp |
