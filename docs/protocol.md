# Device ↔ server protocol

How a mic-assistant device talks to the home server: one WebSocket per button press, carrying the user's audio up and the reply audio down, with explicit push-to-talk turn control.

Status: v1 draft (2026-10-24). Implemented so far by the firmware and the echo server ([server/mic_server.py](../server/mic_server.py)): `hello`, one turn per connection, `reply_start`/`reply_end`, `playback_stopped` (done). The device plays the reply while it streams in (after ~150 ms of buffer), and a press while waiting for or playing a reply interrupts it (new turn on the same connection; replies to older turns are dropped by `turn_id`). Gemini Live bridge: [server/mic_server.py](../server/mic_server.py).

## Design choices

| Choice | Decision | Why |
| --- | --- | --- |
| Transport | WebSocket over plain TCP on the LAN (`ws://`) | On home Wi-Fi a round trip is ~2–10 ms, so TCP + the WebSocket upgrade cost ~2 round trips. 802.11 retransmits lost frames itself, so TCP rarely stalls, and when it does the audio still arrives complete, which speech recognition needs. UDP would add sequencing, loss handling and a reliable control channel for no meaningful gain: latency is measured from release to first reply audio, and audio streams while the user talks. ESP-IDF has a WebSocket client. |
| Encryption | None for v1 | LAN only. A TLS handshake costs a few hundred ms of crypto on the ESP32-S3 at every press. Revisit if the server is ever reachable from outside the LAN. |
| Turn-taking | Explicit, from the button | Press = start of turn, release = end of turn. No voice activity detection anywhere. The mic is only on while S1 is held, and pressing S1 stops playback, so the AI never hears its own reply: no echo cancellation needed. |
| Uplink audio | PCM, 16-bit little-endian, 16 kHz, mono | What the mic produces. ~32 KB/s is nothing for Wi-Fi. No codec to run on the device. |
| Downlink audio | PCM, 16-bit little-endian, mono, at the rate the server names | The realtime AIs return 24 kHz; the device can play that directly instead of the server resampling. |
| Latency budget | Dominated by Wi-Fi association after power-on (1–3 s normally, a few hundred ms with fast-connect), then the AI's own response time. | The protocol itself is not the bottleneck. The device listens ~75 ms after the press and buffers audio in PSRAM until the socket is up. |

## Connection

The device powers on at the press, so every press opens a fresh connection:

```
ws://<server>:<port>/device
Authorization: Bearer <device token>
```

- The server address, port and token are flashed into the firmware for now (see [Wi-Fi and credentials](#wi-fi-and-credentials)). The server starts on a Mac and moves to a Linux home server later, so prefer an mDNS hostname (`<host>.local`) over a raw IP.
- The device does not wait for any reply from the server before streaming. Saving that round trip is the point.
- The server rejects a bad token or version by closing the socket: close code `4001` (unauthorized) or `4002` (unsupported protocol version).

Frames:
- **Text frames** carry JSON control messages, one per frame, each with a `"type"`.
- **Binary frames** carry raw PCM audio and nothing else. Their meaning comes from the control messages around them: binary frames after `turn_start` and before `turn_end` are the user's audio; after `reply_start` and before `reply_end` they're reply audio. WebSocket keeps everything in order, so no sequence numbers are needed.

## One turn

```
press S1 ─▶ device boots, mic starts (~75 ms), audio buffers in PSRAM
            Wi-Fi + WebSocket connect
device ─▶ hello
device ─▶ turn_start
device ─▶ [binary audio: the PSRAM backlog as fast as possible, then live]
release ─▶
device ─▶ turn_end
server ─▶ reply_start
server ─▶ [binary audio]          device starts playing once ~150 ms is buffered
server ─▶ reply_end
device ─▶ playback_stopped (reason "done")
            device powers off
```

### Device → server

| Message | Fields | When |
| --- | --- | --- |
| `hello` | `v` (protocol version, `1`), `device_id`, `firmware`, `battery_mv` | First message on every connection. |
| `turn_start` | `turn_id` (1, 2, … per connection) | The user pressed S1. Audio frames follow. |
| `turn_end` | `turn_id`, `audio_ms` (total audio sent this turn) | The user released S1 (after debounce). |
| `playback_stopped` | `reply_id`, `played_ms`, `reason`: `"done"` or `"interrupted"` | The device stopped playing a reply, either at its end or because the user pressed S1. Sent before the next `turn_start`. |

`played_ms` is how much of the reply actually came out of the speaker: samples the I2S DMA has played, not samples written to it. The device must clamp it to the audio it received for that reply. A known OpenAI integration bug was sending a truncate position beyond the received audio, which the API rejects.

### Server → device

| Message | Fields | When |
| --- | --- | --- |
| `reply_start` | `reply_id`, `turn_id`, `sample_rate` | Reply audio follows. |
| `reply_end` | `reply_id` | The reply is complete. |
| `error` | `code`, `message` | Something failed (the AI is unreachable, the turn was rejected…). The device shows it (LED, a short tone) and powers off. |

After `turn_end`, the device shows a "thinking" LED state until `reply_start`. The wait can be long when the AI calls Home Assistant tools.

### Interrupting a reply

The latch keeps the device powered while a reply plays, so pressing S1 doesn't reboot it; the firmware just sees the button:

```
server ─▶ reply_start, [audio …]
press S1 ─▶ device stops playback immediately (fade out), discards buffered reply audio
device ─▶ playback_stopped (reason "interrupted", played_ms)
device ─▶ turn_start, [audio …]
```

The server tells the AI to cancel the reply and cut its record of the reply at `played_ms`, so the AI's context doesn't contain words the user never heard. Any reply audio still arriving for the old `reply_id` is dropped by the device.

## Server side: mapping to the AI providers

Both main realtime APIs support explicit push-to-talk with their voice activity detection turned off. **Chosen: Gemini Live** (2026-10-24); the OpenAI column stays as the fallback.

| Device event | OpenAI Realtime | Gemini Live |
| --- | --- | --- |
| Session setup | `turn_detection: null` | `automaticActivityDetection.disabled: true`; `activityHandling` left at the default `START_OF_ACTIVITY_INTERRUPTS` |
| `turn_start` | (nothing; start appending) | `activityStart` |
| user audio | `input_audio_buffer.append` | `realtimeInput` audio chunks |
| `turn_end` | `input_audio_buffer.commit`, then `response.create` | `activityEnd` |
| `playback_stopped` interrupted | `response.cancel`, then `conversation.item.truncate` with `audio_end_ms` = `played_ms` | `activityStart` of the next turn already interrupts. I found no truncate equivalent; check the reference before relying on Gemini's context matching what was played |

Notes:
- Sample rates: Gemini Live takes 16 kHz PCM input; OpenAI Realtime's PCM input is 24 kHz, so the server resamples the uplink for OpenAI. Both return 24 kHz PCM, which is why `reply_start` names the rate.
- Re-check field names against the providers' references when implementing; these APIs change often. Community reports say OpenAI's truncation of the transcript is imperfect even with correct `audio_end_ms`.

## Conversation continuity

The device powers off after every reply, so a follow-up question is a new connection. The server keeps each device's AI session open for a while after `reply_end`, keyed by `device_id`. A `hello` from that device within the window continues the same conversation. After the window, the next `hello` starts a fresh one.

Window: 15 minutes (decided 2026-10-24), measured from the end of the last exchange; `MIC_CONVERSATION_SECONDS` on the server changes it. Gemini keeps resumption tokens valid for 2 hours, so anything up to that works. Tokens live in server memory, so a server restart starts fresh conversations. Keeping the device powered for a few seconds after a reply would avoid the reconnect for quick follow-ups, but costs battery (~100 mA while on).

## Failures

| Situation | Device behavior |
| --- | --- |
| Wi-Fi or server not reachable | Keep recording into PSRAM (30 s cap) and keep retrying. If there's still no connection a few seconds after release: error LED/tone, power off. |
| Connection drops mid-turn or mid-reply | The turn is lost (no resume in v1). Error LED/tone, power off. |
| `error` from the server | Error LED/tone, power off. |
| Battery low | Reported in every `hello`; the server can expose it to Home Assistant. Low-battery warnings on the device itself come later. |

## Device-side audio processing (uplink)

Applied before audio is sent:
- DC-blocking high-pass (already in [firmware/main/mic.cpp](../firmware/main/mic.cpp)).
- Drop the ~100 ms mic start-up burst at the start of each recording.
- Gain: speech peaks only reach ~4% of full scale at arm's length, so ~16× (24 dB) fixed gain for now, automatic gain control later.

## Wi-Fi and credentials

For now the Wi-Fi SSID and password, server address and device token are flashed in: `firmware/main/secrets.h`, ignored by git, copied from a committed `secrets.example.h`. The build fails with a pointer to the example if it's missing. A header is used instead of ESP-IDF config options because once a board's `sdkconfig` holds a value, later edits to a defaults file don't override it.

Latency work for Wi-Fi: cache the access point's channel and BSSID in NVS (on-board flash), and use a static IP, to cut association from seconds to a few hundred ms.

Later: a setup flow instead of flashed credentials, likely Improv Wi-Fi (supported by Home Assistant; works over USB serial or BLE), with the result stored in NVS.

## Sources

- OpenAI: [Voice activity detection](https://developers.openai.com/api/docs/guides/realtime-vad.md) (`turn_detection: null` for push-to-talk), [Realtime transcription](https://developers.openai.com/api/docs/guides/realtime-transcription.md) (manual commit)
- OpenAI interruption events (third-party references): [Azure realtime audio reference](https://learn.microsoft.com/de-de/azure/ai-services/openai/realtime-audio-reference), [openai-agents-python #4122](https://github.com/openai/openai-agents-python/pull/4122) (truncate clamping), [forum: truncation leaves unheard text](https://community.openai.com/t/realtime-api-interruptions-dont-properly-trim-the-transcript/1000703)
- Gemini: [Live API WebSockets reference](https://ai.google.dev/api/live), [Live API best practices](https://docs.cloud.google.com/gemini-enterprise-agent-platform/models/live-api/best-practices) (manual activity signals)
