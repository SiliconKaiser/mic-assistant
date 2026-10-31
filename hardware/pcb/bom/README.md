# PCB BOM (v1 draft)

Parts for one device, matching [../netlist.md](../netlist.md). Prices and stock come from the JLCPCB/LCSC catalog, checked 2026-10-24. Prices are unit prices at low quantity and shift often.

| File | What it is |
| --- | --- |
| [bom-full.csv](bom-full.csv) | Everything for one device: board parts, off-board parts, owned and local items. Source column says where each comes from. |
| [bom-jlcpcb.csv](bom-jlcpcb.csv) | JLCPCB assembly upload format (`Comment, Designator, Footprint, LCSC Part #`). Every part soldered to the board. |
| [bom-lcsc.csv](bom-lcsc.csv) | LCSC cart upload: parts bought loose, not soldered by JLCPCB. |

## Summary

| Group | Lines | Parts cost per device |
| --- | --- | --- |
| JLCPCB-assembled | 29 lines, 54 placements | ~$12.30 |
| LCSC loose | 4 lines | ~$1.90 (both test speakers and the optional battery plug) |
| Owned / local / printed | 4×AA holder (Adafruit #830, owned), AA cells, printed button cap | — |

Board fabrication and assembly fees come on top of that. JLCPCB charges a one-off fee per order of about $3 for each unique "extended" part. This board has 12, so about $36, the same whether you order 2 boards or 20. Every resistor, capacitor, diode, MOSFET and the small switches are "basic" parts, with no fee.

## Before ordering

- **Order the LCSC parts first.** Listen to both speakers against the Adafruit #3351. Check that their 1.25 mm plugs fit a PicoBlade-style header. If neither speaker sounds good enough, J3 can change to match whatever speaker wins.
- **Battery lead:** the PHR-2 housing and crimps are only needed if you crimp your own plug. The JST-PH pigtails you already own work too. Check the polarity against J1's silkscreen (pin 1 = +). Q1 makes a reversed plug harmless, but the device won't turn on.
- **Mic stock:** only 555 MP34DT05TR-A were left. If it's out of stock when you order, the LinkMEMS LMD2718T261-OA1 (C5373237) is the fallback. It has a different footprint, so decide before layout.
- **Board count:** JLCPCB assembles at least 2 boards. Multiply the LCSC quantities by the number of devices you plan to build.
- **Designators:** this BOM uses the netlist's refs. Once the KiCad schematic exists, export the JLCPCB BOM and placement file from KiCad, and compare it against this file rather than uploading this one.
