# Desk Gauge – 3D-printed enclosure

Round-screen desk stand for the ESP32-S3 DevKitC-1 (N16R8, 28.08 × 57 mm board)
and the DIYUSER 1.8" round 360×360 ST77916 display (50.6 mm round PCB, 47.9 mm glass).

The front is a round "puck" leaning back 15°, with a slim leg behind it that holds the ESP32.
USB-C exits at the back. Overall size: about 65 × 83 × 74 mm.

## Files

| File | Part | Print orientation | Supports |
|---|---|---|---|
| `1_front_cup_v7_print_face_down.stl` | Front, clean version | Face down | None |
| `1b_front_cup_LOGO_base_print_face_down.stl` | Front, logo version: main colour | Face down | None |
| `1b_front_cup_LOGO_inlay_color2.stl` | Front, logo version: accent colour (text + tech ring) | Same position as the base | None |
| `2_back_and_leg_snapfit_print_upright.stl` | Back of head + leg | Upright, as exported | None |
| `3_base_plate_v9_print_flat.stl` | Base plate with ESP32 cradle | Flat, as exported | None |
| `esp32_round_display_stand.blend` | Editable Blender source | – | – |

Print **either** the clean front **or** the logo front.

### Two-colour logo front (Bambu Studio + AMS)
1. Select both `1b_…` files and drag them in together.
2. Answer **Yes** to "load as a single object with multiple parts". They are already aligned.
3. Give the inlay part the accent colour and the base part the main colour.
4. Print face down. The inlay is 0.6 mm deep, so the colour is in the first 3 layers.

### Recommended settings
- 0.4 mm nozzle, 0.2 mm layers, at least 3 walls
- PETG is recommended for the snap clips (PLA works, but is less forgiving)

## Hardware
No screws are needed. Optional: 4 × 8 mm rubber feet (recesses in the base plate).

## Assembly
1. **Screen → front cup.** Ribbon/pin tab pointing down into the neck. Lay the glass in the
   egg-shaped pocket and press the board down until the three clips click. The two guide ribs
   beside the tab keep the screen from rotating. Rotate the image 180° in firmware if needed.
2. **ESP32 → base plate.** Spread the two posts slightly with your fingers, drop the board in
   (USB end between the posts, against the back), and release. The claws hook over the USB-C
   shells. The front stop holds the board edge without touching the antenna.
3. Plug in the jumper wires. The plugs hang down under the board (about 26 mm of clearance).
4. **Front cup → back part.** Press straight on until the four bridge clips click.
5. **Base plate → leg.** Push up into the leg until the four side clips snap. Inside the leg,
   the walls lock the ESP32 posts, so the board cannot come loose.
6. To open: press the base-plate clips through the side windows. Lever the front off at the
   seam with a plastic pick.

## Key dimensions (from calipers)
- ESP32 board: 28.08 × 57.00 mm (USB-C flush with the board edge), antenna overhang ~7 mm
- Display: round PCB Ø50.61, glass 47.83 × 49.95 mm (egg-shaped toward the ribbon), tab width 29.41 mm

## License
Personal, non-commercial use only: [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).
Print it for yourself, remix it and share your remix under the same terms, but do not sell the
files, printed parts or kits. See [`LICENSE`](LICENSE).
