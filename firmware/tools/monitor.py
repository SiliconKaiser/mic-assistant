#!/usr/bin/env python3
"""Serial monitor for the mic-assistant firmware.

Prints the log with elapsed times and keeps going across resets (the XIAO's USB port drops
and comes back on every reset). Audio blocks sent by main/dump.cpp are saved as .wav files.

Run inside the ESP-IDF environment, which provides pyserial:
    . ~/Projects/esp-idf/esp-idf/export.sh
    python firmware/tools/monitor.py [--port /dev/cu.usbmodem1101] [--seconds 600]

Only one program can hold the port: stop this before flashing.
"""

import argparse
import base64
import datetime
import pathlib
import re
import sys
import time
import wave
import zlib

import serial

REPO = pathlib.Path(__file__).resolve().parents[2]
ANSI = re.compile(rb"\x1b\[[0-9;]*m")


def save_wav(out_dir, rate, data):
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / datetime.datetime.now().strftime("rec-%Y%m%d-%H%M%S.wav")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--seconds", type=float, default=600, help="stop after this long (default 600)")
    ap.add_argument("--out", type=pathlib.Path, default=REPO / "local" / "recordings")
    args = ap.parse_args()

    t0 = time.time()
    port = None
    buf = b""
    wav = None  # (rate, samples, crc, [base64 lines]) while inside a block

    def say(msg):
        print(f"{time.time() - t0:7.1f}s  {msg}", flush=True)

    while time.time() - t0 < args.seconds:
        try:
            if port is None:
                port = serial.Serial(args.port, 115200, timeout=0.2)
                say("[port open]")
            buf += port.read(4096)
        except (serial.SerialException, OSError):
            if port is not None:
                say("[port closed]")
                port = None
                wav = None  # a reset mid-block loses it
            time.sleep(0.1)
            continue

        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = ANSI.sub(b"", raw).decode(errors="replace").rstrip("\r")
            if wav is not None:
                if line == "WAV END":
                    rate, n, crc, lines = wav
                    wav = None
                    try:
                        data = base64.b64decode("".join(lines), validate=True)
                    except ValueError:
                        say("[audio block corrupted: bad base64]")
                        continue
                    if len(data) != 2 * n or zlib.crc32(data) != crc:
                        say(f"[audio block corrupted: {len(data)} of {2 * n} bytes, crc mismatch]")
                        continue
                    path = save_wav(args.out, rate, data)
                    say(f"[saved {n / rate:.2f} s of audio to {path}]")
                else:
                    wav[3].append(line)
                continue
            m = re.fullmatch(r"WAV (\d+) (\d+) ([0-9a-f]{8})", line)
            if m:
                wav = (int(m[1]), int(m[2]), int(m[3], 16), [])
                say(f"[receiving {int(m[2]) / int(m[1]):.2f} s of audio]")
            elif line:
                say(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
