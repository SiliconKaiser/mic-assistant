# Custom PCB

Not started. Work begins once the prototype works.

Plan:

- KiCad project with a schematic and a board layout
- Swap each prototype module for its chips:
  - ESP32-S3 module in place of the XIAO
  - a discrete FET soft-latch (AO3401A + 2N7002) in place of the Pololu switch
  - a buck-boost chip in place of the Pololu regulator
  - the MAX98357A chip directly
  - a MEMS mic
- Keep the prototype's nets and GPIO assignments so the firmware carries over
- Board outline driven by the enclosure (see [../../enclosure/](../../enclosure/))
