# Server

Runs on a computer at home. Status: speaks the device protocol ([../docs/protocol.md](../docs/protocol.md)) with two backends: **gemini** (Gemini Live, the default when `GEMINI_API_KEY` is set) and **echo** (sends each turn back; for testing the device path). Each turn is also saved as a .wav. `--list-live-models` shows which Live models the key can use.

## Run

```sh
python3 -m venv server/.venv && server/.venv/bin/pip install -r server/requirements.txt   # once
server/.venv/bin/python server/mic_server.py        # reads local/server.env
```

`local/server.env` (not in git) holds `MIC_DEVICE_TOKEN` (must match the firmware's `secrets.h`), `MIC_PORT` (8765) and `GEMINI_API_KEY`; see the docstring in [mic_server.py](mic_server.py) for the rest. Turn audio is saved to `local/server-recordings/`. On macOS, allow Python's incoming connections if the firewall asks.

## Run on the Linux server (production)

The server runs on **dell-box** (Ubuntu, `192.168.4.102`, a reserved address; the firmware's `secrets.h` points at it). Checkout: `~/projects/mic-assistant`. It runs as a systemd **user** service, [mic-assistant.service](mic-assistant.service); lingering is on for the account, so it starts at boot without a login. No sudo needed for any of this.

Setup (done 2026-10-24):

```sh
# Ubuntu's python3 lacks venv/ensurepip without sudo apt; uv (one binary in ~/.local/bin) makes the venv instead
~/.local/bin/uv venv --python /usr/bin/python3 server/.venv
~/.local/bin/uv pip install --python server/.venv/bin/python -r server/requirements.txt
scp local/server.env dell-box:projects/mic-assistant/local/   # from the Mac; chmod 600 there
cp server/mic-assistant.service ~/.config/systemd/user/ && systemctl --user enable --now mic-assistant
```

Deploy a change: push from the Mac, then on dell-box

```sh
cd ~/projects/mic-assistant && git pull --ff-only && systemctl --user restart mic-assistant
journalctl --user -u mic-assistant -f        # the log
```

If `requirements.txt` changed, rerun the `uv pip install` line first. If the unit file changed, copy it again and run `systemctl --user daemon-reload`.

## Scope


- Accepts audio streams from devices on the local network
- Bridges them to a realtime voice-to-voice AI: **Google's Gemini Live API** (formerly the Multimodal Live API). A speech-to-text → LLM → text-to-speech pipeline stays the fallback
- Exposes Home Assistant to the AI as tools, through HA's MCP Server integration or its REST/WebSocket API
- Streams the reply audio back to the device
- Includes a laptop-mic test client, so the whole path can be tested without hardware

## Decisions

- **AI provider: Gemini Live** (2026-10-24). Push-to-talk maps onto its manual activity signals; see the mapping in [../docs/protocol.md](../docs/protocol.md).
- **Where it runs:** the dedicated **Linux home server** (dell-box) since 2026-10-24; the Mac is for development. Keep it portable:
  - No macOS-only dependencies or paths (no Core Audio for anything the server itself needs; the laptop-mic test client is the one exception, and it stays a separate tool).
  - Configuration (API key, device tokens, Home Assistant URL and token, listen port) from environment variables or an env file, never hard-coded.
  - Listen on all interfaces, not just localhost, so devices on the LAN can reach it.
  - Runnable as a plain long-lived process, so it can become a systemd service (or a container) on Linux.
- **Device → server address:** the firmware uses dell-box's reserved IP (`192.168.4.102`) from `secrets.h`. An mDNS name (`dell-box.local`) would survive an address change, but it only resolved to IPv6 on this LAN (2026-10-24), and the device needs IPv4.
