# mic-assistant

A battery-powered push-to-talk voice satellite for Home Assistant. Press the button, talk, and hear the reply. Between uses it's fully powered off, so a set of alkaline cells should last years in standby.

The device only captures and plays audio. A server on a computer at home bridges it to a voice-to-voice AI (cloud API for now), and the AI acts on Home Assistant through tool calls.

```
[device] ──Wi-Fi──▶ [home server] ◀──▶ realtime voice AI
                         │
                         └──tool calls──▶ Home Assistant
```

## Goals

- Mic, speaker, status LED, one push-to-talk button
- AA alkaline power, fully off between uses
- Listening in under 300 ms from the button press. The device buffers audio locally while Wi-Fi connects.
- No custom PCB until the prototype works

## Status

| Area | State |
| --- | --- |
| Prototype hardware | Parts ordered 2026-10-18; netlist and assembly guide ready |
| Firmware | Not started |
| Server | Not started |
| PCB | Not started; waits on a working prototype |
| Enclosure | Not started; 3D printed, after the prototype |

## Layout

| Path | Contents |
| --- | --- |
| [docs/](docs/) | Architecture, device–server protocol, design decisions |
| [hardware/prototype/](hardware/prototype/) | Breadboard build: netlist, assembly guide, BOMs |
| [hardware/pcb/](hardware/pcb/) | Custom board (KiCad), later |
| [hardware/datasheets/](hardware/datasheets/) | Index of component datasheets (PDFs kept local) |
| [firmware/](firmware/) | ESP-IDF firmware for the ESP32-S3 |
| [server/](server/) | Home server: device audio ↔ voice AI ↔ Home Assistant |
| [enclosure/](enclosure/) | 3D-printable case |
| `notes/` | Personal notes and photos, not tracked by git |
