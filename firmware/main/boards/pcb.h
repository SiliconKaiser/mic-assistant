// Custom PCB. Keep the prototype's GPIO assignments where possible.
#pragma once

#error "PCB board not defined yet: copy boards/prototype.h and adjust (latch type, divider, mic)"

// Expected differences from the prototype:
//   LATCH_SELF_HOLDING 0  -> discrete FET latch: firmware must drive a HOLD pin high
//                            at the very start of boot and release it to power off.
//   VBAT_DIVIDER_*        -> per the PCB divider.
