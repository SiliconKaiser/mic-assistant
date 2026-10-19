# Server

Runs on a computer at home. Not started.

- Accepts audio streams from devices on the local network
- Bridges them to a realtime voice-to-voice AI (OpenAI Realtime API or Gemini Live API to start; a speech-to-text → LLM → text-to-speech pipeline as an alternative)
- Exposes Home Assistant to the AI as tools, through HA's MCP Server integration or its REST/WebSocket API
- Streams the reply audio back to the device
- Includes a laptop-mic test client, so the whole path can be tested without hardware
