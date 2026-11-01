# Fireplace RF case: print notes for the Bambu H2D

**File:** `FireplaceRF_Case_H2D.3mf` has two separate objects (base and lid), already oriented and laid out side by side on the plate.

## Settings

- **Printer:** Bambu Lab H2D, 0.4 nozzle. One nozzle is enough: this is a single-colour print.
- **Filament:** PETG (for example Bambu PETG HF) or ASA. Not PLA: the case sits near a gas fireplace.
- **Plate:** Textured PEI.
- **Process:** 0.20 mm Standard, 3 wall loops, 15-20% gyroid infill.
- **Supports:** none needed.
  - The USB-C opening (25 mm wide) and the 6 mm antenna hole bridge cleanly in PETG.
  - The hook lips and the lid groove are under 1 mm of overhang.
- **Orientation:** keep it as loaded. The base prints floor-down and the lid prints flat face down, with its ridges up.

## Estimate

About 25 cm³ of plastic, roughly 30 g of PETG in total. The walls are 2 mm, so it prints nearly solid. This is calculated from the mesh volume, not from a slicer run, so re-slice in Bambu Studio for the real time and weight.

## Fit details

| Feature | Size |
|---|---|
| Outside dimensions | about 52 × 64 × 28 mm (base) + 2 mm lid |
| Antenna hole | 6.0 mm. If the SMA connector won't pass, drill it to 6.5 mm (SMA thread is 6.35 mm). |
| USB-C opening | 24.4 × 7.5 mm, exposing both ports |
| Lid | Snaps on with a bead along both long sides |
| Perfboard | 17 × 20 holes, held by 4 snap hooks |

Source model: `fireplace-case.scad` (OpenSCAD). Every dimension is a parameter at the top of the file.
