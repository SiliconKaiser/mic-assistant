#pragma once

namespace battery {

// Battery voltage (after the latch) in millivolts, or -1 on error.
int read_mv();

}  // namespace battery
