# Firmware

ESP-IDF firmware for the XIAO ESP32-S3. Not started.

What it does on each button press:

1. Boot fast: minimal logging, image validation skipped, PSRAM memory test off.
2. Start PDM mic capture into a ring buffer right away, and turn the LED on.
3. Connect to Wi-Fi in parallel, using the stored access point, channel and a static IP.
4. Stream the buffered audio, then live audio, to the server.
5. Play the reply over I2S, with the amp enabled only while audio is flowing.
6. Report battery voltage, then pulse the latch OFF.

Pin map: see [../hardware/prototype/netlist.md](../hardware/prototype/netlist.md). The PDM mic must use I2S port 0, and the amp port 1.

The Arduino test sketches for bring-up are in the [assembly guide](../hardware/prototype/assembly-guide.md).
