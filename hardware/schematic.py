"""Prototype schematic for the PTT voice satellite (breadboard build).

Net-label style: every connection is named, and pins with the same net
label are connected. Render with:

    pip install schemdraw matplotlib
    python hardware/schematic.py        # writes schematic.svg / schematic.png
"""
from pathlib import Path

import schemdraw
import schemdraw.elements as elm

OUT = Path(__file__).parent
STUB = 1.0


def stub(d, anchor, direction, net, length=STUB):
    """Short wire from a pin, ending in a net name."""
    line = elm.Line().at(anchor)
    line = {"left": line.left, "right": line.right, "up": line.up, "down": line.down}[direction](length)
    loc = {"left": "left", "right": "right", "up": "top", "down": "bottom"}[direction]
    d += line.label(net, loc=loc, fontsize=10, color="#1f5fa8")


def module(d, xy, title, left, right, w=4.2):
    pins = [elm.IcPin(name=n, side="left", anchorname=f"L{i}") for i, n in enumerate(left)]
    pins += [elm.IcPin(name=n, side="right", anchorname=f"R{i}") for i, n in enumerate(right)]
    ic = elm.Ic(pins=pins, size=(w, 0.9 * max(len(left), len(right)) + 0.6), pinspacing=0.9,
                edgepadH=0.4, lofst=0.15, plblsize=10).at(xy).theta(0).label(title, loc="top", fontsize=11)
    d += ic
    return ic


def two_terminal(d, xy, part, label, top_net, bot_net, start_at_top=False):
    """Vertical two-terminal part with net names above and below.

    The element's start terminal is at the bottom unless start_at_top is set
    (used for polarized caps, whose + marker sits at the start terminal).
    """
    x, y = xy
    if start_at_top:
        e = part.at((x, y + 2)).down().label(label, loc="bottom", fontsize=10)
    else:
        e = part.at(xy).up().label(label, loc="bottom", fontsize=10)
    d += e
    top, bot = (e.start, e.end) if start_at_top else (e.end, e.start)
    stub(d, top, "up", top_net, 0.5)
    stub(d, bot, "down", bot_net, 0.5)
    return e


with schemdraw.Drawing(show=False) as d:
    d.config(unit=2, fontsize=10)

    # ---------------- Power path ----------------
    d += elm.Label().at((0, 13.5)).label("POWER", fontsize=13, halign="left")

    bt = elm.Battery().at((1, 9.5)).up().label("BT1\n4×AAA clip\n4.0–6.4 V", loc="bottom", fontsize=10)
    d += bt
    stub(d, bt.end, "up", "VBAT", 0.5)
    stub(d, bt.start, "down", "GND", 0.5)

    u1 = module(d, (5, 10.9), "U1  Pololu #2808 latch (LV)",
                ["VIN", "GND", "A"], ["VOUT", "OFF", "ON / CTRL / B"])
    stub(d, u1.L0, "left", "VBAT")
    stub(d, u1.L1, "left", "GND")
    stub(d, u1.L2, "left", "BTN")
    stub(d, u1.R0, "right", "VSW")
    stub(d, u1.R1, "right", "LATCH_OFF")
    stub(d, u1.R2, "right", "n/c")

    u2 = module(d, (18, 10.9), "U2  Pololu S9V11F5 (5 V)",
                ["VIN", "GND", "EN"], ["VOUT"], w=3.6)
    stub(d, u2.L0, "left", "VSW")
    stub(d, u2.L1, "left", "GND")
    stub(d, u2.L2, "left", "n/c (pulled up)")
    stub(d, u2.R0, "right", "5V")

    # Discretes on the power rails
    y = 4.5
    two_terminal(d, (2, y), elm.Capacitor(polar=True), "C1 1000 µF\n10 V\nat U2 VOUT", "5V", "GND", start_at_top=True)
    two_terminal(d, (5, y), elm.Diode(), "D1 1N5817\n(5V → XIAO)", "XIAO_5V", "5V")
    two_terminal(d, (8, y), elm.Capacitor(), "C2 10 µF\n+ C3 0.1 µF\nat XIAO 5V", "XIAO_5V", "GND")
    two_terminal(d, (11, y), elm.Resistor(), "R1 220 k", "VSW", "VBAT_SENSE")
    two_terminal(d, (13.5, y), elm.Resistor(), "R2 100 k", "VBAT_SENSE", "GND")
    two_terminal(d, (16, y), elm.Capacitor(), "C4 0.1 µF", "VBAT_SENSE", "GND")

    # Button + isolation diode + pull-downs
    y = -0.5
    d += elm.Label().at((0, 2.2)).label("BUTTON / CONTROL", fontsize=13, halign="left")
    two_terminal(d, (2, y), elm.Button(), "S1 PTT button", "BTN", "GND")
    two_terminal(d, (5, y), elm.Diode(), "D2 1N4148\n(PTT sense)", "BTN", "PTT")
    two_terminal(d, (8, y), elm.Resistor(), "R3 100 k\nOFF pull-down", "LATCH_OFF", "GND")
    two_terminal(d, (11, y), elm.Resistor(), "R4 10 k\nSD pull-down", "AMP_SD", "GND")
    two_terminal(d, (13.5, y), elm.Capacitor(polar=True), "C5 100 µF\n+ C6 10 µF\nat amp VIN", "5V", "GND", start_at_top=True)

    # ---------------- Logic / audio ----------------
    d += elm.Label().at((0, -4.5)).label("CONTROLLER / AUDIO", fontsize=13, halign="left")

    u3 = module(d, (5, -12), "U3  Seeed XIAO ESP32-S3",
                ["5V", "GND", "3V3", "D0 GPIO1", "D1 GPIO2", "D3 GPIO4", "D2 GPIO3"],
                ["D4 GPIO5", "D5 GPIO6", "D6 GPIO43", "D8 GPIO7", "D7 GPIO44", "D9 GPIO8", "D10 GPIO9"],
                w=5)
    for a, net in [("L0", "XIAO_5V"), ("L1", "GND"), ("L2", "3V3"), ("L3", "VBAT_SENSE"),
                   ("L4", "PTT (pull-up on)"), ("L5", "LATCH_OFF"), ("L6", "n/c (strapping)")]:
        stub(d, getattr(u3, a), "left", net)
    for a, net in [("R0", "I2S_BCLK"), ("R1", "I2S_LRCLK"), ("R2", "I2S_DOUT"), ("R3", "AMP_SD"),
                   ("R4", "LED_DIN"), ("R5", "MIC_CLK"), ("R6", "MIC_DAT")]:
        stub(d, getattr(u3, a), "right", net)

    u4 = module(d, (17, -9.2), "U4  Adafruit MAX98357A #3006",
                ["VIN", "GND", "BCLK", "LRC", "DIN", "SD", "GAIN"], ["SPK+", "SPK−"], w=4)
    for a, net in [("L0", "5V"), ("L1", "GND"), ("L2", "I2S_BCLK"), ("L3", "I2S_LRCLK"),
                   ("L4", "I2S_DOUT"), ("L5", "AMP_SD"), ("L6", "n/c (9 dB)")]:
        stub(d, getattr(u4, a), "left", net)
    spk = elm.Speaker().at(u4.R0).right().anchor("in1").label("LS1 8 Ω 1 W", loc="right", fontsize=10)
    d += spk
    d += elm.Line().at(u4.R1).to(spk.in2)

    u5 = module(d, (17, -16.5), "U5  Adafruit PDM mic #3492",
                ["3V", "GND", "SEL", "CLK", "DAT"], [], w=3.4)
    for a, net in [("L0", "3V3"), ("L1", "GND"), ("L2", "GND (left ch.)"), ("L3", "MIC_CLK"), ("L4", "MIC_DAT")]:
        stub(d, getattr(u5, a), "left", net)

    u6 = module(d, (17, -21.5), "U6  NeoPixel mini #1612",
                ["VCC", "GND", "DIN"], ["DOUT"], w=3.4)
    for a, net in [("L0", "3V3"), ("L1", "GND"), ("L2", "LED_DIN")]:
        stub(d, getattr(u6, a), "left", net)
    stub(d, u6.R0, "right", "n/c")

    d += elm.Label().at((0, -26)).label(
        "Pins with the same net name are connected.  VSW = battery voltage after the latch (zero when off).\n"
        "XIAO 5V pin is fed only through D1; never connect USB 5V to the 5V net.  "
        "Mic and NeoPixel run on the XIAO's 3V3 output.",
        fontsize=9, halign="left")

    d.save(str(OUT / "schematic.svg"))
    d.save(str(OUT / "schematic.png"), dpi=110)
print("ok")
