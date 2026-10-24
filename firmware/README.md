# Firmware

ESP-IDF firmware for the ESP32-S3. One codebase, built per board.

Current state: push-to-talk over Wi-Fi. It holds power, lights the NeoPixel, starts the mic within ~75 ms of the press, joins Wi-Fi and streams the turn to the home server ([docs/protocol.md](../docs/protocol.md)) while S1 is held, plays the server's reply, then powers itself off. On USB it waits for the next S1 press instead. The server bridges to Gemini Live (or echoes, for testing). The reply plays while it streams in.

## Build and flash

Requires ESP-IDF v5.3 (installed at `~/Projects/esp-idf/esp-idf`).

First time: copy [main/secrets.example.h](main/secrets.example.h) to `main/secrets.h` (ignored by git) and fill in the Wi-Fi network (2.4 GHz), the server address and the device token from `local/server.env`. The build stops with a message until it's filled in.

```sh
cd firmware
. ~/Projects/esp-idf/esp-idf/export.sh

idf.py -B build-prototype -DBOARD=prototype build
idf.py -B build-prototype -DBOARD=prototype -p /dev/cu.usbmodem* flash monitor
```

If flashing fails, hold the XIAO's **B** (boot) button while plugging in USB, then retry.

If linking fails with `multiple definition of 'esp_wifi_connect'` (or other undefined/duplicate symbols from prebuilt libraries), the ESP-IDF checkout's submodules don't match it: `git submodule status` in the IDF directory shows them with a `+`. Fix with `git submodule update --init <path>` for the affected ones (2026-10-24: `esp_wifi/lib`, `esp_phy/lib`, `heap/tlsf`, `esp_coex/lib` were off).

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
| [main/led.cpp](main/led.cpp) | Status NeoPixel via the RMT peripheral |
| [main/mic.cpp](main/mic.cpp) | PDM mic on I2S0 into a 30 s PSRAM buffer, with a DC-blocking high-pass |
| [main/wifi.cpp](main/wifi.cpp) | Joins the network from `secrets.h` in the background |
| [main/session.cpp](main/session.cpp) | WebSocket to the home server: protocol messages, reply audio into PSRAM |
| [main/credentials.h](main/credentials.h), [main/secrets.example.h](main/secrets.example.h) | Credentials template; `secrets.h` itself is not in git |
| [main/dump.cpp](main/dump.cpp) | Debug: send a recording over USB as a .wav ([tools/monitor.py](tools/monitor.py)); not called right now, since the server saves each turn |
| [main/main.cpp](main/main.cpp) | Entry point (`extern "C" app_main`): the push-to-talk flow, and the uplink task |

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
- Audio out: avoid clicks by fading in/out (~20 ms) and keeping the amp's SD high through short gaps, sending zeros. Toggling SD pops. Cap the digital volume (`LIMIT` in [main/amp.cpp](main/amp.cpp)): it bounds supply current with the 4 Ω speaker and protects the 8 Ω 1 W one.
- Audio in: the raw PDM output has a large DC offset that drifts for seconds after power-up, and the S3 has no hardware PDM high-pass, so [main/mic.cpp](main/mic.cpp) filters it. Speech peaks only reach ~4% of full scale: apply gain before sending. Drop or fade the ~100 ms start-up burst.
- Timestamps: `esp_timer` starts after the bootloader, so logged times are since app start, not power-on. Measure press-to-X on video (latch LED vs NeoPixel).
- USB logs from the first ~1.5 s after a reset are lost while the port re-enumerates; log summaries at the end. Only one program can hold the serial port: stop any monitor before flashing.
- Power-off retries the OFF pulse, because the latch once ignored one on the bench.
- Cold-boot network time (2026-10-24, Mac server on the LAN), measured per stage (logged in the end-of-run summary) and tuned in [main/wifi.cpp](main/wifi.cpp) and [main/session.cpp](main/session.cpp):
  - Join: a full scan took ~0.7 s; with the cached channel + BSSID it's ~200 ms. Some boots the driver didn't see the AP on its first look and only retried ~2.3 s later; a 400 ms join watchdog now restarts the attempt itself (a restart costs ~120 ms, keeps the cache).
  - IP: DHCP took 1–1.5 s; the static IP from `secrets.h` (backed by a router DHCP reservation) makes it 0.
  - ARP: the Mac often misses the first ARP request (Wi-Fi power save), and lwIP only re-asks once a second, which showed up as ~1 s WebSocket connects. `wifi::warm_arp` re-asks every 50 ms: resolved in 40–400 ms.
  - TCP: the 1.5 s initial retransmission timeout caused 1.5 s stalls right after connecting; it's 300 ms now ([sdkconfig.defaults](sdkconfig.defaults)).
  - Leaving: `wifi::stop()` deauthenticates before power-off, so the AP doesn't see the next join as a stale association.
  - Result (15 cold boots): joined 182–253 ms, WebSocket up 300–570 ms after app start (ARP to the Mac is now the biggest piece, 40–350 ms); release to first echoed audio 24–109 ms.
- Stack: app_main runs the whole flow; the 3.5 KB default overflowed, so it's 8 KB ([sdkconfig.defaults](sdkconfig.defaults)). Large buffers go static or in PSRAM, not on task stacks.
- Boot-time baseline (Arduino sketch, 2026-10-24): ~100–170 ms from latch on to code running, measured on 30 fps video. The target is under 300 ms to listening.
- `CONFIG_BOOT_ROM_LOG_ALWAYS_OFF` is left off deliberately. It burns an eFuse, which is permanent.
- Pin map and wiring: [../hardware/prototype/netlist.md](../hardware/prototype/netlist.md). Arduino bench-test sketches: [assembly guide](../hardware/prototype/assembly-guide.md).
