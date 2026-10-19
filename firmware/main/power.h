#pragma once

#include <stdbool.h>

// Call first thing in app_main: on boards with a non-self-holding latch this keeps power on.
void power_init(void);

// True while the push-to-talk button is held.
bool power_ptt_pressed(void);

// Cut our own power. Does not return on battery; returns only if powered over USB.
void power_off(void);
