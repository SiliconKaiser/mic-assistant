#!/usr/bin/env python3
"""mic-assistant home server: speaks the device protocol in docs/protocol.md.

For now the backend is an echo: each turn's audio is saved as a .wav and sent straight back as
the reply. The Gemini Live bridge will replace EchoBackend.

Portable on purpose (it moves from the Mac to a Linux home server): config comes from the
environment or an env file, it listens on all interfaces, and it runs as a plain long-lived
process (a systemd service later).

    server/.venv/bin/python server/mic_server.py [--env-file local/server.env]

Config (environment variables, or KEY=VALUE lines in the env file):
    MIC_DEVICE_TOKEN   required; the device sends it as "Authorization: Bearer <token>"
    MIC_PORT           default 8765
    MIC_HOST           default 0.0.0.0 (all interfaces)
    MIC_RECORDINGS     where turn audio is saved; default local/server-recordings
"""

import argparse
import asyncio
import datetime
import json
import logging
import os
import pathlib
import time
import wave

from websockets.asyncio.server import ServerConnection, serve

REPO = pathlib.Path(__file__).resolve().parents[1]
UPLINK_RATE = 16000  # device audio: 16-bit little-endian mono PCM
CHUNK_BYTES = 3200   # reply audio frames: 100 ms at 16 kHz

log = logging.getLogger("mic_server")


def load_env_file(path):
    """KEY=VALUE lines; real environment variables win."""
    for line in pathlib.Path(path).read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip())


def save_wav(directory, device_id, turn_id, pcm):
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = directory / f"{stamp}-{device_id}-turn{turn_id}.wav"
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(UPLINK_RATE)
        w.writeframes(pcm)
    return path


class EchoBackend:
    """Replies to every turn with the turn's own audio."""

    async def reply(self, pcm):
        """Returns (sample_rate, pcm) for the reply."""
        return UPLINK_RATE, pcm


class DeviceSession:
    def __init__(self, ws: ServerConnection, backend, recordings):
        self.ws = ws
        self.backend = backend
        self.recordings = recordings
        self.device_id = "unknown"
        self.turn_id = None
        self.turn_audio = bytearray()
        self.next_reply_id = 1
        self.t0 = time.monotonic()

    def ms(self):
        return int((time.monotonic() - self.t0) * 1000)

    async def run(self):
        async for message in self.ws:
            if isinstance(message, bytes):
                if self.turn_id is None:
                    log.warning("%s: audio outside a turn, dropped", self.device_id)
                else:
                    self.turn_audio += message
                continue
            try:
                msg = json.loads(message)
            except ValueError:
                log.warning("%s: bad JSON: %r", self.device_id, message[:80])
                continue
            await self.handle(msg)

    async def handle(self, msg):
        kind = msg.get("type")
        if kind == "hello":
            self.device_id = str(msg.get("device_id", "unknown"))
            log.info("%s: hello at %d ms (firmware %s, battery %s mV)", self.device_id, self.ms(),
                     msg.get("firmware"), msg.get("battery_mv"))
        elif kind == "turn_start":
            self.turn_id = msg.get("turn_id")
            self.turn_audio = bytearray()
            log.info("%s: turn %s started at %d ms", self.device_id, self.turn_id, self.ms())
        elif kind == "turn_end":
            await self.finish_turn(msg)
        elif kind == "playback_stopped":
            log.info("%s: reply %s %s after %s ms played", self.device_id, msg.get("reply_id"),
                     msg.get("reason"), msg.get("played_ms"))
        else:
            log.warning("%s: unknown message %r", self.device_id, kind)

    async def finish_turn(self, msg):
        turn_id, pcm = self.turn_id, bytes(self.turn_audio)
        self.turn_id = None
        seconds = len(pcm) / 2 / UPLINK_RATE
        log.info("%s: turn %s ended at %d ms: %.2f s of audio (device says %s ms)", self.device_id, turn_id,
                 self.ms(), seconds, msg.get("audio_ms"))
        path = save_wav(self.recordings, self.device_id, turn_id, pcm)
        log.info("%s: saved %s", self.device_id, path)

        rate, reply = await self.backend.reply(pcm)
        reply_id = self.next_reply_id
        self.next_reply_id += 1
        await self.ws.send(json.dumps({"type": "reply_start", "reply_id": reply_id, "turn_id": turn_id,
                                       "sample_rate": rate}))
        for i in range(0, len(reply), CHUNK_BYTES):
            await self.ws.send(reply[i:i + CHUNK_BYTES])
        await self.ws.send(json.dumps({"type": "reply_end", "reply_id": reply_id}))
        log.info("%s: sent reply %s (%.2f s at %d Hz) by %d ms", self.device_id, reply_id,
                 len(reply) / 2 / rate, rate, self.ms())


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env-file", default=str(REPO / "local" / "server.env"))
    args = ap.parse_args()
    if pathlib.Path(args.env_file).exists():
        load_env_file(args.env_file)

    token = os.environ.get("MIC_DEVICE_TOKEN")
    if not token:
        raise SystemExit(f"MIC_DEVICE_TOKEN is not set (env or {args.env_file})")
    host = os.environ.get("MIC_HOST", "0.0.0.0")
    port = int(os.environ.get("MIC_PORT", "8765"))
    recordings = pathlib.Path(os.environ.get("MIC_RECORDINGS", REPO / "local" / "server-recordings"))
    backend = EchoBackend()

    async def handler(ws: ServerConnection):
        peer = ws.remote_address[0] if ws.remote_address else "?"
        if ws.request.path != "/device":
            await ws.close(4004, "unknown path")
            return
        if ws.request.headers.get("Authorization") != f"Bearer {token}":
            log.warning("%s: rejected (bad token)", peer)
            await ws.close(4001, "unauthorized")
            return
        log.info("%s: connected", peer)
        session = DeviceSession(ws, backend, recordings)
        try:
            await session.run()
        finally:
            log.info("%s (%s): disconnected after %d ms", session.device_id, peer, session.ms())

    async with serve(handler, host, port, max_size=2**20):
        log.info("listening on ws://%s:%d/device (echo backend), saving to %s", host, port, recordings)
        await asyncio.get_running_loop().create_future()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
