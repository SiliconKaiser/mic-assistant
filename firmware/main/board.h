// Board pin map and per-board constants. Selected by CONFIG_BOARD_*.
#pragma once

#include "sdkconfig.h"

#if CONFIG_BOARD_PROTOTYPE
#include "boards/prototype.h"
#elif CONFIG_BOARD_PCB
#include "boards/pcb.h"
#else
#error "No board selected"
#endif
