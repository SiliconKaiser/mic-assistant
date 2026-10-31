#!/usr/bin/env python3
"""mic-assistant home server: speaks the device protocol in docs/protocol.md.

Each device connection (one per button press) gets a Conversation from a backend:
  - gemini: bridges the turn to the Gemini Live API (push-to-talk via manual activity signals)
  - echo:   saves the turn and sends it straight back (no AI; for testing the device path)

Portable on purpose (it moves from the Mac to a Linux home server): config comes from the
environment or an env file, it listens on all interfaces, and it runs as a plain long-lived
process (a systemd service later).

    server/.venv/bin/python server/mic_server.py [--env-file local/server.env]

Config (environment variables, or KEY=VALUE lines in the env file):
    MIC_DEVICE_TOKEN          required; the device sends it as "Authorization: Bearer <token>"
    MIC_BACKEND               gemini or echo; default gemini if GEMINI_API_KEY is set, else echo
    GEMINI_API_KEY            for the gemini backend
    MIC_GEMINI_MODEL          Live API model; default below (check the current list: they change)
    MIC_CONVERSATION_SECONDS  a press within this long after the last one continues the same
                              conversation (Gemini session resumption); default 900 (15 min)
    MIC_PORT                  default 8765
    MIC_HOST                  default 0.0.0.0 (all interfaces)
    MIC_RECORDINGS            where turn audio is saved; default local/server-recordings
    MIC_DEVICE_SETTINGS       per-device settings (volume), kept across restarts; default
                              local/device-settings.json
    MIC_AUDIENCE              adult or child: child swaps in a prompt for a young child and refuses
                              MIC_SHELL. Default adult
    MIC_SHELL                on: give the AI a run_command tool (shell commands on this machine, no
                              confirmation; meant for a sandboxed server). Default off
    MIC_SHELL_CWD             where commands run; default the home directory
    MIC_SHELL_TIMEOUT         seconds before a command is killed; default 60
"""

import argparse
import array
import asyncio
import datetime
import json
import logging
import os
import pathlib
import signal
import time
import wave

import numpy as np
from scipy.signal import butter, sosfilt, sosfilt_zi
from websockets.asyncio.server import ServerConnection, serve
from websockets.exceptions import ConnectionClosed

REPO = pathlib.Path(__file__).resolve().parents[1]
UPLINK_RATE = 16000        # device audio: 16-bit little-endian mono PCM
MAX_FRAME_BYTES = 4000     # reply frames: under the device's 4096-byte WebSocket buffer
DEFAULT_GEMINI_MODEL = "gemini-3.8-live"  # newest non-preview Live model on 2026-10-24; gemini-3.1-flash-live-preview is the faster-tier option
GEMINI_OUTPUT_RATE = 24000  # Live API output: 16-bit little-endian mono PCM at 24 kHz
SHELL_OUTPUT_CHARS = 6000   # per stream; longer output keeps its start and end

# Reply volume, set by voice (the set_volume tool) and applied here to the reply audio. 100% is
# full scale, i.e. the firmware's VOLUME; each point below is the same step in dB.
DEFAULT_VOLUME = 40        # percent, for a device with no saved setting
MIN_VOLUME = 10            # percent; requests below it (including "mute") stop here
MIN_VOLUME_GAIN = 1 / 40   # reply gain at MIN_VOLUME: -32 dB, ~0.36 dB per point

SYSTEM_INSTRUCTION = """\
You are a voice assistant on a small push-to-talk speaker at home. The user holds a button while \
speaking and hears your reply through a small speaker. Keep replies short and conversational: \
one to three sentences unless asked for more. Never use markdown, lists or symbols that don't \
read aloud well."""

# MIC_AUDIENCE=child: replaces SYSTEM_INSTRUCTION. Written for a five-year-old.
CHILD_INSTRUCTION = """\
You are a friendly talking helper inside a small push-to-talk speaker. The person talking to you \
is a young child, about five years old. They hold a button while speaking and hear your reply \
through a small speaker.

How to talk: use simple words and short sentences a five-year-old understands. Be warm, cheerful, \
patient and encouraging. Keep replies to one or two short sentences unless the child asks for a \
story or a game. Never use markdown, lists or symbols that don't read aloud well. Young children's \
speech can be hard to make out: if you aren't sure what they said, kindly ask them to say it again \
instead of guessing. Silly questions deserve fun, kind answers; never tease or scold.

What you can do: answer curious questions about animals, nature, space, how things work and the \
world; tell short, gentle stories with happy endings; play simple games like riddles, rhymes, \
counting, colors, letters and "guess the animal sound".

Keep everything suitable for a five-year-old: nothing scary, violent, gory, sexual, or about drugs, \
alcohol or weapons, no bad words, and no news that could frighten them. For big or tender topics \
(death, illness, where babies come from, scary things they heard about), give a short, gentle, \
honest answer in simple words and say a grown-up they love can tell them more. If the child asks \
for something unsuitable, say kindly that it's something to talk about with a grown-up.

Safety: never suggest anything dangerous, like playing with fire, matches, knives, medicine, \
cleaning products, electrical sockets, climbing high, going near water, or leaving home or going \
anywhere with someone without a grown-up. If the child says they are hurt, scared, lost, sick or \
that someone is being mean or making them feel unsafe, tell them gently to go and find a grown-up \
they trust right now. Calling them by a first name they tell you is fine, but don't ask for their \
full name, address, school or other private details, and don't repeat them if they share them. \
Never tell them to keep a secret from their grown-ups.

You are a helper in a speaker, not a person: say so simply if asked. You can't buy things, call \
anyone or go anywhere."""

# Appended to either instruction above.
ENDING_INSTRUCTION = """
When you have answered, stop. End on the answer itself, not on a question, an offer or an \
invitation to keep talking (no "Do you like...?", "Want to hear more?", "Anything else?"). Ask a \
question only when you need one to do what was asked: when the request is unclear, or when it's \
your turn in a game the user started, like a riddle or guess-the-animal."""

VOLUME_INSTRUCTION = """
The speaker's volume runs from {min} to 100 percent. When the user asks to change or check it, \
call set_volume before saying anything; for an unspecific "louder" or "quieter", move 10 points. \
Confirm in a few words, like "Volume 40." Never say you changed the volume without calling \
set_volume."""

SHELL_INSTRUCTION = """
You can run shell commands on the home server (Linux) with run_command, and chain several to get \
something done. Run what the user asks without asking for confirmation. Commands run \
non-interactively and are killed after {timeout:g} seconds, so start long jobs in the background \
with all their output in a file (nohup job > file 2>&1 &; anything still writing to the command's \
own output is killed too) and say so. Tell the user what you found or did in plain speech; don't \
read raw output, paths or long numbers aloud unless asked."""

log = logging.getLogger("mic_server")


def load_env_file(path):
    """KEY=VALUE lines; real environment variables win."""
    for line in pathlib.Path(path).read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip())


def save_wav(directory, device_id, turn_id, pcm, suffix=""):
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = directory / f"{stamp}-{device_id}-turn{turn_id}{suffix}.wav"
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(UPLINK_RATE)
        w.writeframes(pcm)
    return path


def volume_gain(percent):
    """Reply gain for a volume setting: MIN_VOLUME_GAIN at MIN_VOLUME up to 1.0 at 100%."""
    return MIN_VOLUME_GAIN ** ((100 - percent) / (100 - MIN_VOLUME))


def scale_pcm(pcm, gain):
    """16-bit little-endian PCM times gain (at most 1.0, so nothing clips)."""
    samples = array.array("h", pcm[:len(pcm) // 2 * 2])  # native order: little-endian on x86 and ARM
    return array.array("h", (round(s * gain) for s in samples)).tobytes()


class UplinkCleaner:
    """Cleans one turn of device audio before the AI hears it. The raw audio is what gets saved as the
    turn's WAV; the cleaned audio is saved next to it as -clean.wav. Streaming: feed() each frame as it
    arrives, flush() at turn_end. Work happens in fixed 10 ms blocks, so the result doesn't depend on how
    the audio arrives, and lags the input by 2 ms plus the rest of a block. From the calibration of
    2026-10-31 (local/calibrations/), three steps:

    1. Pops: single-sample spikes (from ~00:00 on 2026-10-31, 3-58 per second; an electrical fault on
       the prototype, not sound). A sample far off the line through its neighbours is replaced, with
       those neighbours, by a straight line across. Runs longer than POP_MAX_RUN are left alone.
    2. The button's release click: a jump to near full scale out of quiet (speech peaks around a third
       of full scale, and loud speech is already loud before its peaks). Gain dips to CLICK_GAIN until
       the click has died down. The dip starts LOOKAHEAD samples early and ramps, so it doesn't click.
    3. A 4th-order high-pass at HIGH_PASS_HZ: the click's ring is a ~150 Hz resonance of the case, and
       speech recognition doesn't need what's below.
    """

    BLOCK = UPLINK_RATE // 100  # samples worked on at a time
    POP_FACTOR = 32             # spike threshold, in medians of |sample - mean of neighbours| over 100 ms
    POP_FLOOR = 100             # ... but never below this, for near-digital silence
    POP_MAX_RUN = 6             # samples; a longer run is speech, not a pop
    CLICK_TRIGGER = 24000       # |sample| that starts a dip...
    CLICK_QUIET = 14000         # ... if the 20 ms before it (up to 1 ms before) stayed below this
    CLICK_HOLD = 6000           # the dip lasts until 10 ms after the last sample above this...
    CLICK_MAX_MS = 80           # ... or this long at most
    CLICK_GAIN = 0.03           # -30 dB
    LOOKAHEAD = 32              # samples (2 ms)
    HIGH_PASS_HZ = 180

    def __init__(self):
        self.sos = butter(4, self.HIGH_PASS_HZ, "highpass", fs=UPLINK_RATE, output="sos")
        self.zi = sosfilt_zi(self.sos) * 0  # start at rest: the device already removed the DC offset
        self.pending = np.zeros(0)  # input not yet worked on
        self.ctx = np.zeros(2)  # the 2 samples before `pending`, pops removed: left context
        self.e_hist = np.zeros(0)  # |sample - mean of neighbours| over the last 100 ms
        self.history = np.zeros(UPLINK_RATE // 50)  # |sample| of the last 20 ms, for the click trigger
        self.delay = np.zeros(self.LOOKAHEAD)  # pop-free samples waiting for the click stage
        self.skip = self.LOOKAHEAD  # the delay's initial silence isn't audio: dropped from the output
        self.gain = 1.0
        self.n = 0  # samples seen by the click stage
        self.click_start = self.click_last_loud = None

    def feed(self, pcm):
        self.pending = np.concatenate([self.pending, np.frombuffer(pcm[:len(pcm) // 2 * 2], "<i2")])
        out = []
        while len(self.pending) >= self.BLOCK + 2:  # 2 samples of right context for the pop check
            out.append(self._block(self.pending[:self.BLOCK], self.pending[self.BLOCK:self.BLOCK + 2]))
            self.pending = self.pending[self.BLOCK:]
        return self._finish(out)

    def flush(self):
        out = []
        if len(self.pending):
            out.append(self._block(self.pending, np.repeat(self.pending[-1:], 2)))
            self.pending = np.zeros(0)
        out.append(self._duck_clicks(np.zeros(self.LOOKAHEAD)))  # silence pushes out the delayed samples
        return self._finish(out)

    def _finish(self, blocks):
        out = np.concatenate(blocks) if blocks else np.zeros(0)
        cut = min(self.skip, len(out))
        out, self.skip = out[cut:], self.skip - cut
        if not len(out):
            return b""
        out, self.zi = sosfilt(self.sos, out, zi=self.zi)
        return np.clip(np.round(out), -32768, 32767).astype("<i2").tobytes()

    def _block(self, x, right):
        b = np.concatenate([self.ctx, x, right]).astype(np.float64)
        end = 2 + len(x)
        e = np.abs(b[1:-1] - (b[:-2] + b[2:]) / 2)  # e[i] belongs to b[i + 1]
        self.e_hist = np.concatenate([self.e_hist, e[1:end - 1]])[-UPLINK_RATE // 10:]
        threshold = max(self.POP_FACTOR * np.median(self.e_hist), self.POP_FLOOR)
        bad = np.zeros(len(b), bool)
        for i in np.nonzero(e[1:end - 1] > threshold)[0] + 2:
            bad[max(i - 1, 2):min(i + 2, end)] = True  # the decimation filter smears a pop over ~3 samples
        edges = np.diff(np.concatenate([[0], bad.astype(np.int8), [0]]))
        for start, stop in zip(np.nonzero(edges == 1)[0], np.nonzero(edges == -1)[0]):
            if stop - start <= self.POP_MAX_RUN:
                b[start:stop] = np.linspace(b[start - 1], b[stop], stop - start + 2)[1:-1]
        self.ctx = b[end - 2:end]
        return self._duck_clicks(b[2:end])

    def _duck_clicks(self, x):
        """Gain is decided on the newest sample and applied LOOKAHEAD samples behind it."""
        quiet_from, quiet_to = len(self.history), len(self.history) - UPLINK_RATE // 1000
        hold, longest = UPLINK_RATE // 100, UPLINK_RATE * self.CLICK_MAX_MS // 1000
        recent = np.concatenate([self.history, np.abs(x)])
        delayed = np.concatenate([self.delay, x])
        out = np.empty(len(x))
        for j, v in enumerate(recent[quiet_from:]):
            n = self.n + j
            if (self.click_start is None and v >= self.CLICK_TRIGGER
                    and recent[j:j + quiet_to].max() < self.CLICK_QUIET):
                self.click_start = self.click_last_loud = n
            if self.click_start is not None:
                if v >= self.CLICK_HOLD:
                    self.click_last_loud = n
                if n - self.click_last_loud > hold or n - self.click_start > longest:
                    self.click_start = None
            if self.click_start is not None:  # down in 16 samples (1 ms), back up in 32 (2 ms)
                self.gain = max(self.CLICK_GAIN, self.gain - (1 - self.CLICK_GAIN) / 16)
            else:
                self.gain = min(1.0, self.gain + (1 - self.CLICK_GAIN) / 32)
            out[j] = delayed[j] * self.gain
        self.n += len(x)
        self.history = recent[-len(self.history):]
        self.delay = delayed[-self.LOOKAHEAD:]
        return out


class DeviceSettings:
    """Per-device settings that outlive connections and server restarts. For now, the volume."""

    def __init__(self, path):
        self.path = pathlib.Path(path)
        try:
            self.data = json.loads(self.path.read_text())
        except FileNotFoundError:
            self.data = {}

    def volume(self, device_id):
        return self.data.get(device_id, {}).get("volume", DEFAULT_VOLUME)

    def set_volume(self, device_id, percent):
        self.data.setdefault(device_id, {})["volume"] = percent
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.data, indent=2) + "\n")
        tmp.replace(self.path)  # atomic: a crash mid-write can't leave a half-written file


def clip(data):
    text = data.decode(errors="replace")
    if len(text) <= SHELL_OUTPUT_CHARS:
        return text
    half = SHELL_OUTPUT_CHARS // 2
    return f"{text[:half]}\n[... {len(text) - 2 * half} characters cut ...]\n{text[-half:]}"


class Shell:
    """Runs the AI's run_command calls. No confirmation and no filtering: anyone who can talk to the
    device is trusted, and the server runs sandboxed. The server's own settings and secrets (MIC_*,
    GEMINI_*) are left out of the commands' environment."""

    def __init__(self, cwd, timeout):
        self.cwd = cwd
        self.timeout = timeout
        self.env = {k: v for k, v in os.environ.items() if not k.startswith(("MIC_", "GEMINI_"))}

    async def run(self, command):
        proc = await asyncio.create_subprocess_shell(
            command, cwd=self.cwd, env=self.env, stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
            start_new_session=True)  # own process group, so a kill takes its children too
        try:
            out, err = await asyncio.wait_for(proc.communicate(), self.timeout)
        except BaseException as e:  # timed out, or cancelled because the user interrupted
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            await proc.wait()
            if isinstance(e, TimeoutError):
                return {"error": f"timed out after {self.timeout:g} s; killed"}
            raise
        return {"exit_code": proc.returncode, "stdout": clip(out), "stderr": clip(err)}


# --- Backends ---------------------------------------------------------------------------------
#
# A Conversation gets the device's turn as calls (turn_start, audio, turn_end) and answers through
# its sink (the DeviceSession): begin_reply(rate), reply_audio(pcm), end_reply(), error(message).
# Calls must not block for long: the device gives up if its sends stall for ~2 s.


class EchoConversation:
    def __init__(self, sink):
        self.sink = sink
        self.pcm = bytearray()

    async def open(self):
        pass

    async def turn_start(self, turn_id):
        self.pcm = bytearray()

    async def audio(self, pcm):
        self.pcm += pcm

    async def turn_end(self, turn_id):
        await self.sink.begin_reply(UPLINK_RATE)
        await self.sink.reply_audio(bytes(self.pcm))
        await self.sink.end_reply()

    async def interrupted(self, reply_id, heard_ms):
        pass  # the whole echo was already sent

    async def close(self):
        pass


class EchoBackend:
    name = "echo"

    def conversation(self, device_id, sink):
        return EchoConversation(sink)


class GeminiBackend:
    """Gemini Live, push-to-talk: automatic activity detection off, the button marks the turn.

    The device powers off between presses, so each press is a new Live connection. Session
    resumption carries the conversation over: the latest resumption handle is kept per device,
    and a connection within MIC_CONVERSATION_SECONDS of the last one resumes it.
    """

    name = "gemini"

    def __init__(self, api_key, model, conversation_seconds, settings, shell=None, child=False):
        from google import genai  # only needed for this backend

        self.client = genai.Client(api_key=api_key)
        self.model = model
        self.conversation_seconds = conversation_seconds
        self.settings = settings  # DeviceSettings, for the set_volume tool
        self.shell = shell  # a Shell for the run_command tool, or None
        self.child = child  # MIC_AUDIENCE=child
        self.instruction = ((CHILD_INSTRUCTION if child else SYSTEM_INSTRUCTION) + ENDING_INSTRUCTION
                            + VOLUME_INSTRUCTION.format(min=MIN_VOLUME))
        if shell:
            self.instruction += SHELL_INSTRUCTION.format(timeout=shell.timeout)
        self.handles = {}  # device_id -> (handle, monotonic time last updated)

    def handle_for(self, device_id):
        handle, when = self.handles.get(device_id, (None, 0))
        if handle and time.monotonic() - when <= self.conversation_seconds:
            return handle
        return None

    def store_handle(self, device_id, handle):
        self.handles[device_id] = (handle, time.monotonic())

    def forget_handle(self, device_id):
        self.handles.pop(device_id, None)

    def conversation(self, device_id, sink):
        return GeminiConversation(self, device_id, sink)


class GeminiConversation:
    def __init__(self, backend, device_id, sink):
        self.backend = backend
        self.device_id = device_id
        self.sink = sink
        self.outbox = asyncio.Queue()  # ("start" | "audio" | "end", payload), sent once connected
        self.tasks = []
        self.cm = None
        self.session = None
        self.turn_end_at = None
        # Interruption: from a new turn_start until its activityEnd is sent, any model output is
        # the old reply (with manual activity detection Gemini can't answer the new turn before
        # activityEnd), so it's dropped. activityStart itself makes Gemini cancel the old reply.
        self.discarding = False
        self.replying = False
        self.tool_tasks = {}  # function call id -> task running it
        # The current model turn called a tool. That turn ends (turn_complete) at the call, and
        # Gemini speaks in a later turn, once it has the result: the device's reply stays open.
        self.tool_called = False

    async def open(self):
        # Connect in the background, so the device's turn streams in (into the outbox) meanwhile.
        self.tasks.append(asyncio.create_task(self._run()))

    async def turn_start(self, turn_id):
        if self.turn_end_at is not None:  # a previous turn exists: its reply may still be coming
            self.discarding = True
            self.replying = False
        self.outbox.put_nowait(("start", None))

    async def audio(self, pcm):
        self.outbox.put_nowait(("audio", pcm))

    async def turn_end(self, turn_id):
        self.turn_end_at = time.monotonic()
        self.outbox.put_nowait(("end", None))

    async def interrupted(self, reply_id, heard_ms):
        pass  # Gemini cancels on the next activityStart; see turn_start

    async def close(self):
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        if self.cm:
            try:
                await self.cm.__aexit__(None, None, None)
            except Exception:  # the connection may already be gone
                pass

    def _config(self, handle):
        from google.genai import types

        functions = [types.FunctionDeclaration(
            name="set_volume",
            description=f"Set the speaker volume of the device the user is talking on, {MIN_VOLUME}-100 "
                        "percent. Give level for an absolute setting, or change for a relative one in "
                        "percentage points (negative is quieter). Returns the resulting level. change=0 "
                        "just reads it.",
            parameters=types.Schema(type=types.Type.OBJECT, properties={
                "level": types.Schema(type=types.Type.INTEGER, description=f"Absolute volume, {MIN_VOLUME}-100"),
                "change": types.Schema(type=types.Type.INTEGER, description="Relative change, e.g. -10 or 10"),
            }),
        )]
        if self.backend.shell:
            functions.append(types.FunctionDeclaration(
                name="run_command",
                description="Run a shell command on the home server. Returns exit_code, stdout and "
                            "stderr (long output is cut in the middle), or an error.",
                parameters=types.Schema(type=types.Type.OBJECT, required=["command"], properties={
                    "command": types.Schema(type=types.Type.STRING, description="A bash command line"),
                }),
            ))
        return types.LiveConnectConfig(
            response_modalities=[types.Modality.AUDIO],
            system_instruction=self.backend.instruction,
            # Google Search grounding, so questions about current events, weather, etc. get answers.
            tools=[types.Tool(google_search=types.GoogleSearch()), types.Tool(function_declarations=functions)],
            realtime_input_config=types.RealtimeInputConfig(
                automatic_activity_detection=types.AutomaticActivityDetection(disabled=True)),
            session_resumption=types.SessionResumptionConfig(handle=handle),
            context_window_compression=types.ContextWindowCompressionConfig(sliding_window=types.SlidingWindow()),
            input_audio_transcription=types.AudioTranscriptionConfig(),
            output_audio_transcription=types.AudioTranscriptionConfig(),
        )

    async def _connect(self, handle):
        self.cm = self.backend.client.aio.live.connect(model=self.backend.model, config=self._config(handle))
        self.session = await self.cm.__aenter__()

    async def _run(self):
        handle = self.backend.handle_for(self.device_id)
        t0 = time.monotonic()
        try:
            try:
                await self._connect(handle)
            except Exception as e:
                if not handle:
                    raise
                # E.g. 1008 "session is already connected to an existing client": an earlier
                # connection still holds the conversation. An answer without context beats none.
                log.warning("%s: resuming failed (%s); starting a new conversation", self.device_id, e)
                self.backend.forget_handle(self.device_id)
                handle = None
                await self._connect(None)
        except Exception as e:
            log.error("%s: Gemini connect failed: %s", self.device_id, e)
            await self.sink.error(f"AI unavailable: {type(e).__name__}")
            return
        log.info("%s: Gemini connected in %d ms (%s)", self.device_id, (time.monotonic() - t0) * 1000,
                 "resumed conversation" if handle else "new conversation")
        receiver = asyncio.create_task(self._receive())
        self.tasks.append(receiver)
        try:
            await self._send()
        except Exception as e:
            log.error("%s: sending to Gemini failed: %s", self.device_id, e)
            await self.sink.error("AI connection lost")

    async def _send(self):
        from google.genai import types

        while True:
            kind, payload = await self.outbox.get()
            if kind == "start":
                await self.session.send_realtime_input(activity_start=types.ActivityStart())
            elif kind == "audio":
                await self.session.send_realtime_input(
                    audio=types.Blob(data=payload, mime_type=f"audio/pcm;rate={UPLINK_RATE}"))
            elif kind == "end":
                await self.session.send_realtime_input(activity_end=types.ActivityEnd())
                self.discarding = False  # from here on, output answers this turn
                self.tool_called = False  # nor does a tool call from an interrupted reply count

    async def _receive(self):
        heard, said = [], []
        try:
            while True:
                # receive() ends after each turn_complete; keep listening for the next turn.
                async for msg in self.session.receive():
                    update = msg.session_resumption_update
                    if update and update.resumable and update.new_handle:
                        self.backend.store_handle(self.device_id, update.new_handle)
                    if msg.go_away:
                        log.info("%s: Gemini go_away, %s left", self.device_id, msg.go_away.time_left)
                    if msg.tool_call:
                        for call in msg.tool_call.function_calls or []:
                            task = asyncio.create_task(self._call_tool(call))
                            self.tool_tasks[call.id] = task
                            self.tasks.append(task)
                            self.tool_called = True
                    if msg.tool_call_cancellation:
                        for call_id in msg.tool_call_cancellation.ids or []:
                            task = self.tool_tasks.pop(call_id, None)
                            if task:
                                task.cancel()
                    content = msg.server_content
                    if not content:
                        continue
                    if content.input_transcription and content.input_transcription.text:
                        heard.append(content.input_transcription.text)
                    if content.output_transcription and content.output_transcription.text:
                        said.append(content.output_transcription.text)
                    if self.discarding:
                        if content.interrupted or content.turn_complete:
                            log.info('%s: old reply %s; heard "%s", said so far "%s"', self.device_id,
                                     "interrupted" if content.interrupted else "finished",
                                     "".join(heard).strip(), "".join(said).strip())
                            heard, said = [], []
                        continue  # the interrupted reply's leftovers
                    if content.model_turn:
                        for part in content.model_turn.parts or []:
                            if part.inline_data and part.inline_data.data:
                                if not self.replying:
                                    self.replying = True
                                    wait = (time.monotonic() - self.turn_end_at) * 1000 if self.turn_end_at else -1
                                    log.info("%s: first reply audio %d ms after turn_end", self.device_id, wait)
                                    await self.sink.begin_reply(GEMINI_OUTPUT_RATE)
                                await self.sink.reply_audio(part.inline_data.data)
                    if content.interrupted:
                        log.info("%s: Gemini reply interrupted", self.device_id)
                    if content.turn_complete:
                        if self.tool_called:  # Gemini answers after the tool's result: keep waiting
                            self.tool_called = False
                            continue
                        if not self.replying:  # nothing to say: still close the turn for the device
                            await self.sink.begin_reply(GEMINI_OUTPUT_RATE)
                        await self.sink.end_reply()
                        self.replying = False
                        log.info('%s: heard "%s"', self.device_id, "".join(heard).strip())
                        log.info('%s: said  "%s"', self.device_id, "".join(said).strip())
                        heard, said = [], []
        except asyncio.CancelledError:
            raise
        except Exception as e:
            log.info("%s: Gemini receive ended: %s", self.device_id, e)

    async def _call_tool(self, call):
        """Runs one function call in its own task, so receiving goes on (a cancellation can arrive)."""
        from google.genai import types

        t0 = time.monotonic()
        if call.name == "run_command" and self.backend.shell:
            command = (call.args or {}).get("command", "")
            log.info("%s: $ %s", self.device_id, command)
            try:
                result = await self.backend.shell.run(command)
            except asyncio.CancelledError:
                log.info("%s: command cancelled after %d ms", self.device_id, (time.monotonic() - t0) * 1000)
                raise
            except Exception as e:  # e.g. MIC_SHELL_CWD doesn't exist
                result = {"error": f"{type(e).__name__}: {e}"}
            log.info("%s: command done in %d ms: %s", self.device_id, (time.monotonic() - t0) * 1000,
                     f"exit {result['exit_code']}" if "exit_code" in result else result["error"])
        elif call.name == "set_volume":
            result = self._set_volume(call.args or {})
        else:
            result = {"error": f"unknown function {call.name}"}
        self.tool_tasks.pop(call.id, None)
        try:
            await self.session.send_tool_response(
                function_responses=[types.FunctionResponse(id=call.id, name=call.name, response=result)])
        except Exception as e:
            log.info("%s: sending the result of %s failed: %s", self.device_id, call.name, e)

    def _set_volume(self, args):
        """set_volume: stored before the result goes back, so the spoken confirmation, which Gemini
        only starts once it has the result, already plays at the new volume."""
        settings = self.backend.settings
        old = settings.volume(self.device_id)
        try:
            if args.get("level") is not None:
                wanted = round(float(args["level"]))
            else:
                wanted = old + round(float(args.get("change") or 0))
        except (TypeError, ValueError):
            return {"error": "level and change must be numbers"}
        new = min(100, max(MIN_VOLUME, wanted))
        if new != old:
            settings.set_volume(self.device_id, new)
        log.info("%s: volume %d%% -> %d%% (asked %s)", self.device_id, old, new, dict(args))
        result = {"volume": new}
        if new != wanted:
            result["limited_to"] = "minimum" if new == MIN_VOLUME else "maximum"
        return result


# --- Device side ------------------------------------------------------------------------------


class DeviceSession:
    def __init__(self, ws: ServerConnection, backend, recordings, live, settings):
        self.ws = ws
        self.backend = backend
        self.recordings = recordings
        self.live = live  # device_id -> its current DeviceSession
        self.settings = settings
        self.device_id = "unknown"
        self.conversation = None
        self.turn_id = None
        self.turn_audio = bytearray()  # as the device sent it
        self.clean_audio = bytearray()  # what the AI hears
        self.cleaner = None
        self.next_reply_id = 1
        self.reply_id = None
        self.reply_turn = 0  # turn the next reply answers
        self.reply_bytes = 0
        self.reply_rate = 0
        self.t0 = time.monotonic()

    def ms(self):
        return int((time.monotonic() - self.t0) * 1000)

    async def run(self):
        try:
            async for message in self.ws:
                if isinstance(message, bytes):
                    if self.turn_id is None:
                        log.warning("%s: audio outside a turn, dropped", self.device_id)
                    else:
                        self.turn_audio += message
                        await self.send_clean(self.cleaner.feed(message))
                    continue
                try:
                    msg = json.loads(message)
                except ValueError:
                    log.warning("%s: bad JSON: %r", self.device_id, message[:80])
                    continue
                await self.handle(msg)
        except ConnectionClosed:
            pass  # the device went away mid-turn, or a newer connection replaced this one
        finally:
            if self.live.get(self.device_id) is self:
                del self.live[self.device_id]
            if self.conversation:
                await self.conversation.close()

    async def replaced(self):
        """A newer connection from the same device took over. If the device lost power or reset
        mid-turn, this connection would otherwise linger until keepalive gives up (~50 s), and its
        Gemini session blocks resuming the conversation."""
        conversation, self.conversation = self.conversation, None
        if conversation:
            await conversation.close()
        asyncio.create_task(self.ws.close(4000, "replaced by a newer connection"))  # the peer may be gone

    async def handle(self, msg):
        kind = msg.get("type")
        if kind == "hello":
            self.device_id = str(msg.get("device_id", "unknown"))
            log.info("%s: hello at %d ms (firmware %s, battery %s mV, volume %d%%)", self.device_id, self.ms(),
                     msg.get("firmware"), msg.get("battery_mv"), self.settings.volume(self.device_id))
            stale = self.live.get(self.device_id)
            if stale is not None and stale is not self:
                log.info("%s: replacing its earlier connection, still open after %d ms", self.device_id, stale.ms())
                await stale.replaced()
            self.live[self.device_id] = self
            self.conversation = self.backend.conversation(self.device_id, self)
            await self.conversation.open()
        elif kind == "turn_start":
            self.turn_id = msg.get("turn_id")
            self.reply_turn = self.turn_id
            self.turn_audio = bytearray()
            self.clean_audio = bytearray()
            self.cleaner = UplinkCleaner()
            log.info("%s: turn %s started at %d ms", self.device_id, self.turn_id, self.ms())
            if self.conversation:
                await self.conversation.turn_start(self.turn_id)
        elif kind == "turn_end":
            if self.cleaner:
                await self.send_clean(self.cleaner.flush())
            turn_id, pcm = self.turn_id, bytes(self.turn_audio)
            self.turn_id = None
            log.info("%s: turn %s ended at %d ms: %.2f s of audio", self.device_id, turn_id, self.ms(),
                     len(pcm) / 2 / UPLINK_RATE)
            save_wav(self.recordings, self.device_id, turn_id, pcm)
            save_wav(self.recordings, self.device_id, turn_id, bytes(self.clean_audio), "-clean")
            if self.conversation:
                await self.conversation.turn_end(turn_id)
        elif kind == "playback_stopped":
            log.info("%s: reply %s %s after %s ms played", self.device_id, msg.get("reply_id"),
                     msg.get("reason"), msg.get("played_ms"))
            if msg.get("reason") == "interrupted" and self.conversation:
                await self.conversation.interrupted(msg.get("reply_id"), msg.get("played_ms"))
        else:
            log.warning("%s: unknown message %r", self.device_id, kind)

    async def send_clean(self, pcm):
        if pcm:
            self.clean_audio += pcm
            if self.conversation:
                await self.conversation.audio(pcm)

    # Reply sink, used by the conversation.

    async def begin_reply(self, rate):
        self.reply_id = self.next_reply_id
        self.next_reply_id += 1
        self.reply_bytes = 0
        self.reply_rate = rate
        await self.ws.send(json.dumps({"type": "reply_start", "reply_id": self.reply_id,
                                       "turn_id": self.reply_turn, "sample_rate": rate}))

    async def reply_audio(self, pcm):
        # The volume is read per chunk, so a change applies from the next chunk on, mid-reply too.
        pcm = scale_pcm(pcm, volume_gain(self.settings.volume(self.device_id)))
        for i in range(0, len(pcm), MAX_FRAME_BYTES):
            await self.ws.send(pcm[i:i + MAX_FRAME_BYTES])
        self.reply_bytes += len(pcm)

    async def end_reply(self):
        await self.ws.send(json.dumps({"type": "reply_end", "reply_id": self.reply_id}))
        log.info("%s: sent reply %s (%.2f s at %d Hz) by %d ms", self.device_id, self.reply_id,
                 self.reply_bytes / 2 / max(1, self.reply_rate), self.reply_rate, self.ms())

    async def error(self, message):
        try:
            await self.ws.send(json.dumps({"type": "error", "code": "backend", "message": message}))
        except Exception:
            pass


def make_backend(settings):
    choice = os.environ.get("MIC_BACKEND") or ("gemini" if os.environ.get("GEMINI_API_KEY") else "echo")
    if choice == "echo":
        return EchoBackend()
    if choice == "gemini":
        key = os.environ.get("GEMINI_API_KEY")
        if not key:
            raise SystemExit("MIC_BACKEND=gemini needs GEMINI_API_KEY")
        audience = os.environ.get("MIC_AUDIENCE", "adult")
        if audience not in ("adult", "child"):
            raise SystemExit(f"unknown MIC_AUDIENCE {audience!r} (adult or child)")
        child = audience == "child"
        shell = None
        if os.environ.get("MIC_SHELL", "off") == "on":
            if child:
                raise SystemExit("MIC_SHELL=on is not allowed with MIC_AUDIENCE=child")
            shell = Shell(os.path.expanduser(os.environ.get("MIC_SHELL_CWD", "~")),
                          float(os.environ.get("MIC_SHELL_TIMEOUT", "60")))
        return GeminiBackend(key, os.environ.get("MIC_GEMINI_MODEL", DEFAULT_GEMINI_MODEL),
                             float(os.environ.get("MIC_CONVERSATION_SECONDS", "900")), settings, shell, child)
    raise SystemExit(f"unknown MIC_BACKEND {choice!r} (gemini or echo)")


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env-file", default=str(REPO / "local" / "server.env"))
    ap.add_argument("--list-live-models", action="store_true",
                    help="print the Gemini models this API key can use with the Live API, then exit")
    args = ap.parse_args()
    if pathlib.Path(args.env_file).exists():
        load_env_file(args.env_file)
    if args.list_live_models:
        from google import genai

        client = genai.Client(api_key=os.environ.get("GEMINI_API_KEY"))
        for m in client.models.list():
            if "bidiGenerateContent" in (m.supported_actions or []):
                print(m.name.removeprefix("models/"), "-", m.display_name)
        return

    token = os.environ.get("MIC_DEVICE_TOKEN")
    if not token:
        raise SystemExit(f"MIC_DEVICE_TOKEN is not set (env or {args.env_file})")
    host = os.environ.get("MIC_HOST", "0.0.0.0")
    port = int(os.environ.get("MIC_PORT", "8765"))
    recordings = pathlib.Path(os.environ.get("MIC_RECORDINGS", REPO / "local" / "server-recordings"))
    settings = DeviceSettings(os.environ.get("MIC_DEVICE_SETTINGS", REPO / "local" / "device-settings.json"))
    backend = make_backend(settings)
    live = {}

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
        session = DeviceSession(ws, backend, recordings, live, settings)
        try:
            await session.run()
        finally:
            log.info("%s (%s): disconnected after %d ms", session.device_id, peer, session.ms())

    async with serve(handler, host, port, max_size=2**20):
        detail = ""
        if isinstance(backend, GeminiBackend):
            detail = f", model {backend.model}"
            if backend.child:
                detail += ", child audience"
            if backend.shell:
                detail += f", shell commands in {backend.shell.cwd}"
        log.info("listening on ws://%s:%d/device (%s backend%s), saving to %s", host, port, backend.name, detail,
                 recordings)
        await asyncio.get_running_loop().create_future()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    logging.getLogger("websockets").setLevel(logging.WARNING)
    logging.getLogger("httpx").setLevel(logging.WARNING)
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
