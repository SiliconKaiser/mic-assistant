# Custom PCB

Status: netlist and BOM drafted (2026-10-24); the KiCad project isn't started yet.

- [netlist.md](netlist.md): every net, pin by pin, plus parts, layout notes and the firmware changes
- [bom/](bom/): full BOM, the JLCPCB assembly BOM, and the LCSC BOM for off-board parts

Plan:

- KiCad project with a schematic and a board layout, entered from the netlist
- Each prototype module is swapped for its chip:
  - ESP32-S3-WROOM-1-N8R8 module in place of the XIAO
  - MCU-held FET latch (AO3401A + 2N7002) plus reverse-polarity protection in place of the Pololu switch
  - TPS63070 buck-boost in place of the Pololu regulator, plus an AP2112K 3.3 V LDO
  - The MAX98357A chip directly
  - MP34DT05 MEMS mic
- The prototype's nets and GPIO assignments stay, so the firmware carries over
- Board outline driven by the enclosure (see [../../enclosure/](../../enclosure/)). The enclosure needs rework for a board mounted under the top with the switch, LED and mic on it.
