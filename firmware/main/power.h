#pragma once

namespace power {

// Call first thing in app_main: on boards with a non-self-holding latch this keeps power on.
// Also leaves the amp pins floating (see power.cpp).
void init();

// True while the push-to-talk button is held.
bool ptt_pressed();

// True while VSW is live: the latch is on, so the 5V rail and the amp have power.
// False on USB with the latch off.
bool vsw_present();

// Float the amp's pins (BCLK, LRCLK, DIN, SD). Done at init, and by amp::play() afterward.
void release_amp_pins();

// Cut our own power. Does not return on battery; returns only if powered over USB.
void off();

}  // namespace power
