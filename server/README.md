# Server

Runs on a computer at home. Status: speaks the device protocol ([../docs/protocol.md](../docs/protocol.md)) with two backends: **gemini** (Gemini Live, the default when `GEMINI_API_KEY` is set) and **echo** (sends each turn back; for testing the device path). Each turn is also saved as a .wav. `--list-live-models` shows which Live models the key can use.

## Run

```sh
python3 -m venv server/.venv && server/.venv/bin/pip install -r server/requirements.txt   # once
server/.venv/bin/python server/mic_server.py        # reads local/server.env
```

`local/server.env` (not in git) holds `MIC_DEVICE_TOKEN` (must match the firmware's `secrets.h`), `MIC_PORT` (8765) and `GEMINI_API_KEY`; see the docstring in [mic_server.py](mic_server.py) for the rest. Turn audio is saved to `local/server-recordings/`. On macOS, allow Python's incoming connections if the firewall asks.

## Scope


- Accepts audio streams from devices on the local network
- Bridges them to a realtime voice-to-voice AI: **Google's Gemini Live API** (formerly the Multimodal Live API). A speech-to-text → LLM → text-to-speech pipeline stays the fallback
- Exposes Home Assistant to the AI as tools, through HA's MCP Server integration or its REST/WebSocket API
- Streams the reply audio back to the device
- Includes a laptop-mic test client, so the whole path can be tested without hardware

## Decisions

- **AI provider: Gemini Live** (2026-10-24). Push-to-talk maps onto its manual activity signals; see the mapping in [../docs/protocol.md](../docs/protocol.md).
- **Where it runs:** developed and tested on the Mac for now, but it will move to a dedicated **Linux home server**. Keep it portable from the start:
  - No macOS-only dependencies or paths (no Core Audio for anything the server itself needs; the laptop-mic test client is the one exception, and it stays a separate tool).
  - Configuration (API key, device tokens, Home Assistant URL and token, listen port) from environment variables or an env file, never hard-coded.
  - Listen on all interfaces, not just localhost, so devices on the LAN can reach it.
  - Runnable as a plain long-lived process, so it can become a systemd service (or a container) on Linux.
- **Device → server address:** the server's address changes when it moves to the Linux box. Prefer flashing a hostname the device resolves via mDNS (`<host>.local`; ESP-IDF has an mDNS component) over a raw IP, or at least keep the address in the firmware's `secrets.h` so moving is a reflash, not a code change.
