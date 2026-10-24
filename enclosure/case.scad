// mic-assistant tabletop case, v1 (concept: dimensions marked MEASURE are placeholders).
//
// Parts (set `part`, or pass -D 'part="shell"' to the openscad CLI):
//   "assembly"  everything in place (preview only)
//   "section"   the assembly cut through the button, to check how the layers stack
//   "parts"     all printable parts in print orientation, side by side
//   "shell"     top + walls, print upside down (top face on the bed)
//   "base"      bottom plate with battery pocket and protoboard rails, print as is
//   "button"    push-to-talk cap, print top face down
//   "holder"    tactile switch + LED holder, screws under the top, print as is
//   "strap"     speaker retaining strap, print flat
//
// Layout: the battery holder lies on the base; the Perma-Proto board sits on two rails above it;
// the speaker faces up against the top at the back, the button is at the front. Everything that
// touches the case (speaker, button, LED, mic) connects to the board by JST plug, so the shell
// lifts off. 4 screws from below hold base and shell together.
//
// Axes: x = width (left/right), y = depth (front = -y), z = up. Origin: center of the footprint
// at the bottom.

part = "assembly";

$fn = 48;

// ---- Print and fit ---------------------------------------------------------------------------
wall = 2.4;          // side walls
top_t = 2.4;         // top plate
floor_t = 3.0;       // base plate
corner_r = 6;        // outer corner radius
fit = 0.3;           // clearance for parts that slide or drop into each other

// Screws: M3 self-tapping into printed pilot holes (or M3 heat-set inserts: widen pilots).
pilot_d = 2.5;
screw_clear_d = 3.4;
screw_head_d = 6.2;
column_d = 7;        // corner screw columns inside the shell

// ---- Parts ------------------------------------------------------------------------------------
// Battery holder, Adafruit #830 (4xAA, switch, cover): 69.9 x 63.5 x 19.1 mm nominal, +-2.5. MEASURE
bat = [64.5, 70.5, 19.6];      // lying flat, rotated so its long side runs front to back
// Perma-Proto half-size #1609: 81 x 51 mm, 2 mounting holes 73.7 mm apart (assumed centered on the
// short axis). Board thickness and hole diameter: MEASURE.
pcb = [81, 51, 1.6];
pcb_hole_dx = 73.7;
pcb_hole_d = 3.2;               // MEASURE
pcb_pins_below = 2.5;           // solder joints under the board
parts_h = 18;                   // tallest part above the board (C1, 1000 uF?). MEASURE
parts_h_back = 10;              // tallest part in the strip under the speaker. MEASURE (keep C1 out of it)
// Speaker, Adafruit #3351 (4 ohm 3 W enclosed): 70 x 30 x 17, mounting holes 63 x 24, d 3.4.
spk = [70, 30, 17];
spk_holes = [63, 24];
spk_hole_d = 3.4;
// Tactile switch with round cap (Fabian's): ~10 mm cap. All MEASURE.
sw_body = 12;                   // body width (square)
sw_body_h = 3.6;                // body height, without the cap
sw_total_h = 7.5;               // board side of the body to the top of the cap
sw_cap_d = 10;
// NeoPixel mini PCB: MEASURE
led = [10, 10, 3.2];
// PDM mic breakout, Adafruit #3492: 14.0 x 12.8 x 2.8. Which side the sound port is on: MEASURE.
mic = [14.0, 12.8, 2.8];
mic_port_d = 1.6;
// XIAO USB-C, at the left end of the protoboard. Height above the board top: MEASURE.
usb_cut = [12.5, 7.0];          // fits a USB-C plug's overmold
usb_z_above_pcb = 5.1;

// ---- Button -----------------------------------------------------------------------------------
btn_d = 34;                     // visible cap
btn_stem_d = 24;                // stem through the top
btn_rise = 2.0;                 // cap underside above the top surface when released
btn_sleeve = 4;                 // guide sleeve below the top
btn_flange = 1.4;               // retaining flange under the sleeve
btn_pocket = 1.2;               // pocket over the switch cap

// ---- Derived ----------------------------------------------------------------------------------
inner = [pcb.x + 2, 30 + 40 + 4 + 5];   // speaker 30 + button area 40 + gaps
outer = [inner.x + 2 * wall, inner.y + 2 * wall];

z_bat_top = floor_t + bat.z;
z_pcb = z_bat_top + 1.0 + pcb_pins_below;  // board underside
z_pcb_top = z_pcb + pcb.z;
spk_y = inner.y / 2 - 1 - spk.y / 2;      // speaker center, at the back
btn_y = -inner.y / 2 + 3 + btn_d / 2;     // button center, at the front

// Height: whichever needs more room, the speaker over the board's back strip or the button
// stack over the tall parts.
btn_stack = btn_sleeve + 0.8 + btn_flange - btn_pocket + sw_total_h + 1.6;  // below the top's underside
z_top_under = max(z_pcb_top + parts_h_back + 1 + spk.z, z_pcb_top + parts_h + 1 + btn_stack);
height = z_top_under + top_t;

echo(str("case outer: ", outer.x, " x ", outer.y, " x ", height, " mm"));

// ---- Helpers ----------------------------------------------------------------------------------
module rounded_box(size, r, h) {
    linear_extrude(h)
        offset(r) square([size.x - 2 * r, size.y - 2 * r], center = true);
}

column_pos = [for (sx = [-1, 1], sy = [-1, 1])
              [sx * (inner.x / 2 - column_d / 2), sy * (inner.y / 2 - column_d / 2)]];
spk_hole_pos = [for (sx = [-1, 1], sy = [-1, 1]) [sx * spk_holes.x / 2, spk_y + sy * spk_holes.y / 2]];
strap_post_x = spk.x / 2 + 3.5;
holder_screw_dx = btn_stem_d / 2 + 6;
mic_pos = [-inner.x / 2 + 12, -inner.y / 2 + 10];
led_offset = sw_body / 2 + led.x / 2 + 1;

// ---- Shell: top + walls ------------------------------------------------------------------------
module shell() {
    difference() {
        union() {
            difference() {
                rounded_box(outer, corner_r, height);
                translate([0, 0, -1]) rounded_box(inner, max(corner_r - wall, 0.5), z_top_under + 1);
            }
            // Corner screw columns, from the base up to the top.
            for (p = column_pos) translate([p.x, p.y, floor_t]) cylinder(d = column_d, h = z_top_under - floor_t);
            // Button guide sleeve.
            translate([0, btn_y, z_top_under - btn_sleeve])
                cylinder(d = btn_stem_d + 2 * fit + 3.2, h = btn_sleeve);
            // Switch holder bosses.
            for (sx = [-1, 1]) translate([sx * holder_screw_dx, btn_y, z_top_under - btn_stack + 1.6])
                cylinder(d = 6, h = btn_stack - 1.6);
            // Speaker locating pins (into its mounting holes) and strap posts.
            for (p = spk_hole_pos) translate([p.x, p.y, z_top_under - 4]) cylinder(d = spk_hole_d - 0.4, h = 4);
            for (sx = [-1, 1]) translate([sx * strap_post_x, spk_y, z_top_under - spk.z])
                cylinder(d = 6, h = spk.z);
            // Mic pocket frame.
            translate([mic_pos.x, mic_pos.y, z_top_under - mic.z]) difference() {
                cube([mic.x + 2 * fit + 3, mic.y + 2 * fit + 3, mic.z * 2], center = true);
                cube([mic.x + 2 * fit, mic.y + 2 * fit, mic.z * 2 + 1], center = true);
            }
        }
        // Screw pilots in the columns (from below).
        for (p = column_pos) translate([p.x, p.y, floor_t - 0.1]) cylinder(d = pilot_d, h = 14);
        // Button hole through the top and sleeve.
        translate([0, btn_y, z_top_under - btn_sleeve - 1]) cylinder(d = btn_stem_d + 2 * fit, h = btn_sleeve + top_t + 2);
        // Holder and strap pilots.
        for (sx = [-1, 1]) translate([sx * holder_screw_dx, btn_y, z_top_under - btn_stack]) cylinder(d = pilot_d, h = 8);
        for (sx = [-1, 1]) translate([sx * strap_post_x, spk_y, z_top_under - spk.z - 0.1]) cylinder(d = pilot_d, h = 10);
        // Speaker grille: holes over the cone area, clear of the locating pins.
        for (gx = [-24 : 4 : 24], gy = [-8 : 4 : 8])
            translate([gx + (abs(gy) % 8 == 4 ? 2 : 0), spk_y + gy, z_top_under - 1]) cylinder(d = 2.6, h = top_t + 2, $fn = 16);
        // Mic port.
        translate([mic_pos.x, mic_pos.y, z_top_under - 1]) cylinder(d = mic_port_d, h = top_t + 2, $fn = 16);
        // USB-C cutout in the left wall, at the XIAO's port.
        translate([-outer.x / 2 - 1, 0, z_pcb_top + usb_z_above_pcb])
            rotate([0, 90, 0]) linear_extrude(wall + 3)
                offset(1.5) square([usb_cut.y - 3, usb_cut.x - 3], center = true);
    }
}

// ---- Base -------------------------------------------------------------------------------------
rail_y = 40;  // rail length under each end of the protoboard
module base() {
    difference() {
        union() {
            // Plate, inside the walls.
            rounded_box([inner.x - 2 * fit, inner.y - 2 * fit], max(corner_r - wall - fit, 0.5), floor_t);
            // Battery pocket: low walls on the front and back sides.
            for (sy = [-1, 1]) translate([0, sy * (bat.y / 2 + fit + 0.8), floor_t + 2])
                cube([bat.x * 0.6, 1.6, 4], center = true);
            // Protoboard rails under the board's ends.
            for (sx = [-1, 1]) translate([sx * pcb_hole_dx / 2, 0, floor_t + (z_pcb - floor_t) / 2])
                cube([5, rail_y, z_pcb - floor_t], center = true);
        }
        // Screw holes into the shell's columns, countersunk from below.
        for (p = column_pos) {
            translate([p.x, p.y, -1]) cylinder(d = screw_clear_d, h = floor_t + 2);
            translate([p.x, p.y, -0.01]) cylinder(d1 = screw_head_d, d2 = screw_clear_d, h = 1.6);
        }
        // Protoboard screw pilots in the rails.
        for (sx = [-1, 1]) translate([sx * pcb_hole_dx / 2, 0, z_pcb - 8]) cylinder(d = pilot_d, h = 9);
    }
}

// ---- Button cap -------------------------------------------------------------------------------
// Upright, in place: the cap's underside sits btn_rise above the top surface.
module button() {
    stem_len = top_t + btn_sleeve + btn_rise + 0.8;  // 0.8: travel and slack below the sleeve
    difference() {
        union() {
            // Visible cap, slightly domed.
            translate([0, 0, stem_len]) hull() {
                cylinder(d = btn_d, h = 2.4);
                translate([0, 0, 2.4]) cylinder(d = btn_d - 6, h = 1.6);
            }
            // Stem, and the retaining flange at its bottom.
            cylinder(d = btn_stem_d, h = stem_len);
            translate([0, 0, -btn_flange]) cylinder(d = btn_stem_d + 3, h = btn_flange);
        }
        // Pocket over the switch's round cap.
        translate([0, 0, -btn_flange - 0.1]) cylinder(d = sw_cap_d + 0.6, h = btn_pocket + 0.1);
        // Hollow the stem to save plastic and let the LED light the cap from below.
        translate([0, 0, -btn_flange + btn_pocket + 1]) cylinder(d = btn_stem_d - 3.2, h = stem_len - 1);
    }
}

// ---- Switch + LED holder -------------------------------------------------------------------------
module holder() {
    difference() {
        hull() for (sx = [-1, 1]) translate([sx * holder_screw_dx, 0, 0]) cylinder(d = 8, h = 1.6 + sw_body_h);
        // Switch pocket (body), pins through the floor.
        translate([0, 0, 1.6]) cube([sw_body + 2 * fit, sw_body + 2 * fit, sw_body_h * 2], center = true);
        cube([sw_body - 2, sw_body - 2, 10], center = true);
        // LED pocket beside the switch, facing up into the button.
        translate([led_offset, 0, 1.6 + sw_body_h - led.z]) translate([-led.x / 2 - fit, -led.y / 2 - fit, 0])
            cube([led.x + 2 * fit, led.y + 2 * fit, 10]);
        for (sx = [-1, 1]) translate([sx * holder_screw_dx, 0, -1]) cylinder(d = screw_clear_d, h = 10);
    }
}

// ---- Speaker strap ----------------------------------------------------------------------------
module strap() {
    difference() {
        hull() for (sx = [-1, 1]) translate([sx * strap_post_x, 0, 0]) cylinder(d = 8, h = 3);
        for (sx = [-1, 1]) translate([sx * strap_post_x, 0, -1]) cylinder(d = screw_clear_d, h = 5);
    }
}

// ---- Assembly ---------------------------------------------------------------------------------
// With cut = true, the shell is cut away at x = 0 (through the button); viewed from the right
// this shows how the parts inside stack. (Only the shell is cut: OpenSCAD previews draw every
// cut face in one color, so cutting the parts too makes them indistinguishable.)
module part_view(c, cut) {
    color(c) if (cut) intersection() { children(); color(c) translate([-200, -200, -10]) cube([200, 400, 300]); }
    else children();
}

module assembly(cut = false) {
    // Printed parts.
    part_view("whitesmoke", cut) shell();
    part_view("lightblue", false) base();
    part_view("orange", false) translate([0, btn_y, z_top_under - btn_sleeve - 0.8]) button();
    part_view("tomato", false) translate([0, btn_y, z_top_under - btn_stack]) holder();
    part_view("tomato", false) translate([0, spk_y, z_top_under - spk.z - 3]) strap();
    // Stand-ins for the bought parts.
    part_view("dimgray", false) translate([0, 0, floor_t + bat.z / 2]) cube(bat, center = true);
    part_view("seagreen", false) translate([0, 0, z_pcb + pcb.z / 2]) cube(pcb, center = true);
    part_view("khaki", false)   // tallest parts (C1...), kept out of the strip under the speaker
        translate([0, -pcb.y / 2 + 13, z_pcb_top + parts_h / 2]) cube([20, 20, parts_h], center = true);
    part_view("black", false) translate([0, spk_y, z_top_under - spk.z / 2]) cube(spk, center = true);
    part_view("silver", false) translate([-pcb.x / 2 + 10.5, 0, z_pcb_top + 2.5]) cube([21, 17.8, 3.5], center = true);
    part_view("purple", false) translate([mic_pos.x, mic_pos.y, z_top_under - mic.z / 2]) cube(mic, center = true);
    sw_bottom = z_top_under - btn_stack + 1.6;
    part_view("dimgray", false) translate([0, btn_y, sw_bottom + sw_body_h / 2]) cube([sw_body, sw_body, sw_body_h], center = true);
    part_view("gold", false) translate([0, btn_y, sw_bottom]) cylinder(d = sw_cap_d, h = sw_total_h);
    part_view("cyan", false) translate([led_offset, btn_y, sw_bottom + sw_body_h - led.z / 2]) cube(led, center = true);
}

// ---- Output -----------------------------------------------------------------------------------
// All printable parts, in print orientation, side by side (preview only).
module parts_layout() {
    color("whitesmoke") translate([0, 0, 0]) translate([0, 0, height]) rotate([180, 0, 0]) shell();
    color("lightblue") translate([outer.x + 10, 0, 0]) base();
    color("orange") translate([-outer.x / 2 - 30, 25, 0]) rotate([180, 0, 0])
        translate([0, 0, -(top_t + btn_sleeve + btn_rise + 0.8 + 4)]) button();
    color("tomato") translate([-outer.x / 2 - 30, -15, 0]) holder();
    color("tomato") translate([-outer.x / 2 - 30, -40, 0]) rotate([0, 0, 90]) strap();
}

if (part == "assembly") assembly();
else if (part == "parts") parts_layout();
else if (part == "section") assembly(cut = true);
else if (part == "shell") translate([0, 0, height]) rotate([180, 0, 0]) shell();
else if (part == "base") base();
else if (part == "button") translate([0, 0, 0]) rotate([180, 0, 0]) translate([0, 0, -(top_t + btn_sleeve + btn_rise + 0.8 + 4)]) button();
else if (part == "holder") holder();
else if (part == "strap") strap();
