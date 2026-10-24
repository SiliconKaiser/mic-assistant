# Enclosure

3D-printed tabletop case, written as parametric OpenSCAD: [case.scad](case.scad). All dimensions are named at the top of the file.

Status: **v1 concept** (2026-10-24). The layout and stacking are checked in renders, but several part dimensions are placeholders marked `MEASURE` in `case.scad`. Measure those before the first print (see the list below).

About 88 × 84 × 63 mm. The battery holder lies on the base, the Perma-Proto board sits on two rails above it, the speaker faces up under a grille at the back, and a large push-to-talk button sits at the front.

## Parts

| Part | Prints | Notes |
| --- | --- | --- |
| `shell` | Upside down (top face on the bed), no supports | Top and walls in one piece: speaker grille, mic port and pocket, button guide sleeve, corner screw columns, USB-C cutout in the left wall |
| `base` | As is | Bottom plate: battery holder pocket, two rails carrying the protoboard, countersunk screws into the shell |
| `button` | Top face down | Large cap. A pocket under it sits over the tactile switch's round cap. Print in white or translucent so the LED next to the switch lights it up as the status light |
| `holder` | As is | Holds the tactile switch and the NeoPixel under the button; screws to two bosses under the top |
| `strap` | Flat | Holds the speaker against the top; screws to two posts |

Everything that touches the case (speaker, button, LED, mic) connects to the board with a JST-PH plug, so the shell lifts off the base.

## Hardware

| Qty | Part | For |
| --- | --- | --- |
| 4 | M3 × 8 countersunk, self-tapping into the printed pilots | Base to shell |
| 2 | M3 × 6 | Protoboard to the base rails |
| 2 | M3 × 6 | Switch holder |
| 2 | M3 × 6 | Speaker strap |

Pilot holes are 2.5 mm for M3 self-tapping screws. For M3 heat-set inserts, widen `pilot_d`.

## Measurements still needed (`MEASURE` in case.scad)

- Tactile switch: body width and height, total height to the top of the round cap, cap diameter, travel
- NeoPixel module: size and height with wires
- Mic breakout (14.0 × 12.8 × 2.8): which side the sound port is on
- XIAO USB-C: height of the port's center above the protoboard, once soldered
- Protoboard: tallest part (probably C1), and the tallest part in the back strip under the speaker
- Battery holder (#830): actual size (±2.5 mm tolerance on the datasheet)
- Perma-Proto: mounting hole diameter
- Wi-Fi antenna: size and cable length (it goes on a side wall, away from the batteries and the speaker magnet; no case feature yet)

## Build and preview

```sh
OS=/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD
cd enclosure
$OS -o build/shell.stl -D 'part="shell"' case.scad                      # likewise base, button, holder, strap
$OS -o build/section.png --imgsize=1300,900 --viewall --autocenter \
    --camera=0,0,0,90,0,90,0 --projection=ortho -D 'part="section"' case.scad
```

`part` also takes `assembly` (everything in place), `section` (the shell cut away to show how the parts stack) and `parts` (all printable parts in print orientation). Output goes to `build/`, which git ignores.
