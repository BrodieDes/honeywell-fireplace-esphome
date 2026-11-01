// Fireplace RF case: Freenove ESP32-S3 WROOM + Ebyte E07-M1101D (CC1101) on a
// 17 x 20 hole perfboard. Print in PETG or ASA. Render with part = "base" / "lid".
//
// Coordinates: interior origin at the inside corner of the connector end wall,
// left wall and floor. X = width, Y = length (connector end at Y = 0), Z = up.
// Perfboard rows run 0 (antenna end) to 19 (connector end); columns 0 (left) to 16.

part = "base";            // "base", "lid" or "assembly"
$fn = 48;

// ---- Measured parts ----
pitch = 2.54;
pb_cols = 17;  pb_rows = 20;
pb_w = pb_cols * pitch;   // 43.18
pb_l = pb_rows * pitch;   // 50.8
pb_t = 1.6;
header_h = 8.5;           // female header body
pin_plastic = 2.5;        // male header spacer under each module
esp_w = 27.8;  esp_l = 50.6;  esp_ant = 6;  esp_pin_inset = 1.2;
usb_from_left = [7.8, 19.7];   // USB-C centres from the ESP32's left edge (from photo, +/-1 mm)
usb_h = 3.26;
cc_w = 15;  cc_l = 28;

// ---- Case ----
wall = 2;  floor_t = 2;  lid_t = 2;
gap = 0.3;                // perfboard to connector wall
pb_side = 2.2;            // room each side for the snap hooks
pb_z = 4;                 // perfboard underside above the floor (solder side clearance)
in_w = pb_w + 2 * pb_side;
in_l = gap + pb_l + esp_ant + 2.9;
in_h = 26;

antenna_hole_d = 6.0;     // as measured. SMA thread is nominally 6.35 mm: drill out if tight
usb_open_w = 13;          // per plug overmould
usb_open_h = 7.5;

function col(c) = pb_side + pitch / 2 + c * pitch;
function row(r) = gap + pitch / 2 + (pb_rows - 1 - r) * pitch;

pb_top = pb_z + pb_t;
module_bottom = pb_top + header_h + pin_plastic;    // underside of both modules
esp_top = module_bottom + 1.6;
esp_left = col(6) - esp_pin_inset;                  // ESP32 header rows in columns 6 and 16
usb_z = esp_top + usb_h / 2;
sma_x = col(2.5);                                   // CC1101 header in columns 1-4, rows 9-10
sma_z = module_bottom + 0.8;

posts = [[1, 1], [0, 18], [11, 1], [11, 18]];       // free holes: route wires around these
hook_rows = [4, 15];

module base() {
  difference() {
    translate([-wall, -wall, -floor_t])
      cube([in_w + 2 * wall, in_l + 2 * wall, floor_t + in_h]);
    cube([in_w, in_l, in_h + 1]);
    // USB-C: one opening for both ports
    translate([esp_left + usb_from_left[0] - usb_open_w / 2, -wall - 1, usb_z - usb_open_h / 2])
      cube([usb_from_left[1] - usb_from_left[0] + usb_open_w, wall + 2, usb_open_h]);
    // Antenna SMA
    translate([sma_x, 1, sma_z]) rotate([90, 0, 0]) cylinder(d = antenna_hole_d, h = wall + 2);
    // Lid snap grooves along the long walls
    for (x = [-0.5, in_w - 0.5])
      translate([x, 4, in_h - 1.8]) cube([1, in_l - 8, 0.8]);
    // Keyholes for wall mounting (M3/#6 pan head)
    for (y = [in_l * 0.25, in_l * 0.75]) translate([in_w / 2, y, -floor_t - 1]) {
      cylinder(d = 7.5, h = floor_t + 2);
      hull() for (dy = [0, 6]) translate([0, dy, 0]) cylinder(d = 3.6, h = floor_t + 2);
    }
  }
  // Support posts under the perfboard
  for (p = posts) translate([col(p[0]), row(p[1]), 0]) cylinder(d = 3.5, h = pb_z);
  // Snap hooks holding the perfboard's long edges
  for (r = hook_rows) {
    translate([pb_side, row(r), 0]) hook();
    translate([in_w - pb_side, row(r), 0]) mirror([1, 0, 0]) hook();
  }
}

// Cantilever arm standing in the side gap; the lip catches the perfboard edge.
module hook() {
  arm = 1.0;  lip = 0.8;  w = 7;
  translate([-0.2 - arm, -w / 2, 0]) {
    cube([arm, w, pb_top + 1.4]);
    // lip with a lead-in ramp above it
    translate([arm, 0, pb_top])
      rotate([-90, 0, 0]) linear_extrude(w)
        polygon([[0, 0], [lip, 0], [0, -1.4]]);
  }
}

module lid() {
  lip = 1.2;  lip_h = 3;  clr = 0.2;
  translate([-wall, -wall, 0]) cube([in_w + 2 * wall, in_l + 2 * wall, lid_t]);
  // Lips on the long walls only: the end walls carry the connector openings
  for (side = [0, 1]) {
    x = side == 0 ? clr : in_w - clr - lip;
    translate([x, 5, -lip_h]) {
      cube([lip, in_l - 10, lip_h]);
      // snap bead into the wall groove
      bx = side == 0 ? 0 : lip;
      translate([bx, 0, 1.8 - 0.8 + 0.4]) rotate([-90, 0, 0]) cylinder(d = 0.8, h = in_l - 10, $fn = 16);
    }
  }
}

// Ghost boards for checking fit (assembly view only)
module boards() {
  color("tan", 0.6) translate([pb_side, gap, pb_z]) cube([pb_w, pb_l, pb_t]);
  color("red", 0.6) translate([esp_left, gap + (pb_l - esp_l) / 2, module_bottom]) {
    cube([esp_w, esp_l, 1.6]);
    translate([(esp_w - 18) / 2, esp_l, 0]) cube([18, esp_ant, 3.1]);
    for (u = usb_from_left) translate([u - 4.5, 0, 1.6]) cube([9, 7.5, usb_h]);
  }
  color("blue", 0.6) translate([sma_x - cc_w / 2, gap, module_bottom]) {
    cube([cc_w, cc_l, 1.6]);
    translate([cc_w / 2, 0, 0.8]) rotate([90, 0, 0]) cylinder(d = 6.35, h = 9.45);
  }
}

if (part == "base") base();
else if (part == "lid") rotate([180, 0, 0]) lid();   // printed upside down, flat face on the bed
else { base(); boards(); translate([0, 0, in_h]) %lid(); }
