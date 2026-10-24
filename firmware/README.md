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
| [sdkconfig.defaults](sdkconfig.defaults) | Settings for all boards: fast boot, USB console, PSRAM without the boot memory test, no C++ exceptions/RTTI |
| [main/board.h](main/board.h) | Picks the board's pin map |
| [main/power.cpp](main/power.cpp) | Latch control, PTT reading, VSW check, amp-pin safety. Handles both a self-holding latch (prototype) and a hold-pin latch (PCB) |
| [main/battery.cpp](main/battery.cpp) | Battery voltage through the divider (ADC calibrated; divider not yet) |
| [main/main.cpp](main/main.cpp) | Entry point (`extern "C" app_main`) |

## C++ conventions

The firmware is C++ used as "C with classes": ESP-IDF's C APIs are called directly, and only things with a lifetime (I2S channels, buffers, sockets, ADC handles) get a small owning class so they're released on every return path.

- No exceptions or RTTI (set in [sdkconfig.defaults](sdkconfig.defaults)). Errors stay `esp_err_t` return values.
- Audio buffers go in PSRAM via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`. Plain `new` and STL containers allocate from internal RAM; keep `std::vector`, `std::string` and `std::function` out of the audio path.
- No global objects with constructors: they run before `app_main`, cost boot time, and run in no defined order. Create real objects inside `app_main`.
- Designated initializers must follow the struct's declaration order in C++ (C doesn't care), so some ESP-IDF example code needs reordering. Leaving fields out is fine; [main/CMakeLists.txt](main/CMakeLists.txt) silences the warning C++ gives for it.
- Pins and board constants are `constexpr` in `namespace board`; use `if constexpr` instead of `#if` for board differences.

## Notes

- The PDM mic must use I2S port 0, and the amp port 1.
- The amp is powered only while the latch is on, even on USB. Check `power::vsw_present()` before driving any amp pin; otherwise the amp back-powers through its inputs and crackles.
- Audio out: avoid clicks by fading in/out (~20 ms) and keeping the amp's SD high through short gaps, sending zeros. Toggling SD pops. Cap the digital volume for the 8 Ω 1 W speaker.
- Audio in: speech at arm's length only reaches ~1.5% of full scale, so apply digital gain before sending it.
- Power-off retries the OFF pulse, because the latch once ignored one on the bench.
- Boot-time baseline (Arduino sketch, 2026-10-24): ~100–170 ms from latch on to code running, measured on 30 fps video. The target is under 300 ms to listening.
- `CONFIG_BOOT_ROM_LOG_ALWAYS_OFF` is left off deliberately. It burns an eFuse, which is permanent.
- Pin map and wiring: [../hardware/prototype/netlist.md](../hardware/prototype/netlist.md). Arduino bench-test sketches: [assembly guide](../hardware/prototype/assembly-guide.md).
