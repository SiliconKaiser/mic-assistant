# Firmware

ESP-IDF firmware for the ESP32-S3. One codebase, built per board.

Current state: a skeleton. It holds power, logs boot time, battery voltage and PTT state, blinks, then powers itself off. Audio, Wi-Fi and the server protocol are next.

## Build and flash

Requires ESP-IDF v5.3 (installed at `~/Projects/esp-idf/esp-idf`).

```sh
cd firmware
. ~/Projects/esp-idf/esp-idf/export.sh

idf.py -B build-prototype -DBOARD=prototype build
idf.py -B build-prototype -DBOARD=prototype -p /dev/cu.usbmodem* flash monitor
```

If flashing fails, hold the XIAO's **B** (boot) button while plugging in USB, then retry.

Each board gets its own build directory and `sdkconfig.<board>`, so switching boards never mixes settings.

## Boards

| Board | Selected by | Files |
| --- | --- | --- |
| Breadboard prototype (XIAO ESP32-S3, Pololu latch) | `-DBOARD=prototype` | [boards/prototype/sdkconfig.defaults](boards/prototype/sdkconfig.defaults), [main/boards/prototype.h](main/boards/prototype.h) |
| Custom PCB | `-DBOARD=pcb` | Placeholder; fails the build on purpose until it's defined |

To add a board: create `boards/<name>/sdkconfig.defaults`, add a `CONFIG_BOARD_<NAME>` choice in [main/Kconfig.projbuild](main/Kconfig.projbuild), and add `main/boards/<name>.h` with the pin map.

## Layout

| File | Purpose |
| --- | --- |
| [sdkconfig.defaults](sdkconfig.defaults) | Settings for all boards: fast boot, USB console, PSRAM without the boot memory test |
| [main/board.h](main/board.h) | Picks the board's pin map |
| [main/power.c](main/power.c) | Latch control and PTT reading. Handles both a self-holding latch (prototype) and a hold-pin latch (PCB) |
| [main/battery.c](main/battery.c) | Calibrated battery voltage through the divider |
| [main/main.c](main/main.c) | Entry point |

## Notes

- The PDM mic must use I2S port 0, and the amp port 1.
- `CONFIG_BOOT_ROM_LOG_ALWAYS_OFF` is left off deliberately. It burns an eFuse, which is permanent.
- Pin map and wiring: [../hardware/prototype/netlist.md](../hardware/prototype/netlist.md). Arduino bench-test sketches: [assembly guide](../hardware/prototype/assembly-guide.md).
