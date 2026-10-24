#pragma once

#include <cstddef>
#include <cstdint>

// One WebSocket session with the home server, per docs/protocol.md. (Not named `link`: that
// collides with POSIX link().)
namespace session {

// Connect to the server in secrets.h. False on failure or timeout. Call after Wi-Fi is up.
bool open(int timeout_ms);

// Send a JSON control message, or user audio (16-bit mono PCM) as a binary frame.
bool send_text(const char *json);
bool send_audio(const int16_t *samples, size_t n);

// Wait for a reply to start (reply_start). False on timeout, an `error` message, or disconnect.
bool wait_reply_start(int timeout_ms);
// The reply has fully arrived (reply_end), or the session failed.
bool reply_complete();
bool failed();

// The last reply's audio, kept until the next open(). It grows while the reply streams in:
// play up to reply_sample_count() and poll for more.
const int16_t *reply_samples();
size_t reply_sample_count();
int reply_sample_rate();
int reply_id();
// esp_timer time (us since app start) when reply audio started arriving, or 0.
int64_t reply_first_audio_us();

void close();

// esp_timer times (us since app start) for the last open(): called, TCP connected (the
// client's BEFORE_CONNECT fires just before connecting), WebSocket handshake done.
struct Timing {
    int64_t open;
    int64_t arp_done;  // server's MAC resolved (see wifi::warm_arp)
    int arp_misses;    // ARP requests that went unanswered first; -1 = never resolved
    int64_t before_connect;
    int64_t connected;
};
Timing timing();

}  // namespace session
