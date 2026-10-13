# Layout skins and the panel

There are **six** layouts. Block frames are not a separate skin but a setting ("Block frames": on/off/as in the skin)
that applies to any skin.

Skin data: [`app/src/main/assets/skins/*.json`](../app/src/main/assets/skins/), format:
[`_schema.md`](../app/src/main/assets/skins/_schema.md). The "one Canvas + data" decision: [ADR-0004](adr/0004-panel-canvas-skins.md).
Screenshots of every layout: [README](../README.md#six-layouts).

## 1. What changes and what does not

- **What changes** is the placement of the four blocks of the top zone: `system` (MASTER/SELECT, PRESETS/ALGORITHM,
  OCT−/OCT+), `buttons` (12 buttons 2×6), `knobs` (KNOB 1–4), `screen` (the 240×240 LCD).
- **What does not change** is the keyboard: 27 keys in two rows (11 "black" on top, 16 "white" below), **across the
  full screen width**, deliberately stretched for fingers.

| id | Left | Centre | Right |
|---|---|---|---|
| `classic` | system | buttons over knobs | screen |
| `knobs-top` | system | knobs over buttons | screen |
| `screen-left` | screen | buttons over knobs | system |
| `screen-left-knobs-top` | screen | knobs over buttons | system |
| `screen-center` | system | screen | knobs over buttons |
| `screen-center-buttons-top` | system | screen | buttons over knobs (like the original, but the transport above the encoders) |

A new skin = a new JSON (a user skin can be imported too: Settings → Skin → "Import skin").

## 2. The layout engine

1. Safe area and symmetric margins:
   - The top zone uses `m = max(insetLeft, insetRight)` on **both** sides, so the top panel is strictly centred
     whichever side the cutout is on (a "drop" / punch hole on the left or right in landscape).
   - The keyboard takes the full screen width minus a small symmetric margin `2 * u` (`widthPx - 4 * u`), because the
     camera cutout in landscape sits in the middle of the short edge and does not reach the keyboard zone at the
     bottom.
   - The skin's inner horizontal padding is symmetric: `padding: [18, 14, 18, 8]`.
2. `u = safeHeight / 440`. All paddings and gaps in a skin are in u.
3. Keyboard height = max(`keyboardFraction` × height, `minKeyboardDp`). The rest is the top zone.
4. The top zone is laid out by a `row/column` tree by weights (`weight`) with gaps (`gap`). The skins' weights are
   tuned so the screen gets the largest square available (weight 0.31).
5. The LCD takes the largest square that fits in its block; the bezel is `max(3 * u, side / 48)`.
6. Settings is a **small dot in the top-left corner**. It sits in the panel's margin and takes no space from any
   block. Its touch target is a square in the screen corner (in landscape the cutout is in the middle of the short
   side, so the corner is free): the largest from 40 down to 28 dp that touches no knob, button, LCD or keyboard
   (`SkinLayout.fitGearTarget`; host test). On screens with rounded corners the dot moves diagonally inside the
   visible arc (the corner radius comes from `Display.getRoundedCorner`, Android 12+; Samsung M52 — 84 px, POCO C75 —
   64 px), otherwise the glass would cut it off.
7. The result is a list of rectangles/circles with roles (`Button(FX)`, `Knob(K2)`, `Key(14)`, `Screen`, `Gear`) —
   for drawing and for hit-testing alike.

Approximate sizes:

| | Samsung M52 | POCO C75 |
|---|---|---|
| px (landscape) | 2400 × 1080 | 1640 × 720 |
| dp (≈) | 914 × 411 (420 dpi, 2.625×) | 820 × 360 (320 dpi, 2.0×) |
| keyboard 36 % | ≈148 dp → rows ≈70 dp | ≈130 dp → rows ≈62 dp |
| white key pitch | ≈57 dp | ≈51 dp |
| 2×6 button | ≈56 × 115 dp | ≈51 × 100 dp |
| screen (LCD square) | 602 px (229 dp, 2.5× of 240) | 402 px (201 dp, 1.7× of 240) |

The minimum touch target is 40 dp (on the C75 with 360 dp of height this holds for every element except OCT± — their
strip stretches over the block's full width — and the settings dot: a corner target ≥ 28 dp).

## 3. Blocks

One style for every skin — the look of the browser emulator (`upstream/felucca/web/emu/index.html`). Shared code:
`render/PanelStyle.kt`. The body is one colour (#1D1D1F); dark "islands" (#141415) sit under the buttons, under OCT,
under the keys and as a well around each knob; nothing is lighter than the body. Sizes are ours (from `u` and the
layout), proportions are the emulator's.

**system**: 2×2 knobs right on the body, labels above them in the DotGothic16 font (OFL, as in the emulator). A knob:
a dark well `r·29/24`, a flat cap #2A2A2D with a light rim #3A3A3E. MASTER is a potentiometer, its pointer always
visible (−135…+135°); SELECT/PRESETS/ALGORITHM are endless encoders, the pointer shows only while touched. At the
bottom of the block — its own island with the two buttons OCT− / OCT+.

**buttons**: 2 rows × 6 in a dark tray (grooves between buttons), fixed order, labels from the firmware
(`button_names`: SLOOP has SEL instead of SCL): `FX SCL ENV LFO EDIT GLO` / `HOME SAVE ARP SEQ PLAY·STOP REC`. REC
has a red label, PLAY/STOP two lines; the label size is the same for every button, OCT included. LED states follow the
emulator's `drawLights()`: normal #2A2A2D, dim (available) #3A3934, lit — cream #FFF4D6 with a dark label, breath —
breathing dim…#D8CFB6 with a 1.13 s period (lower peak for DIM LO), PLAY green — #5FE08A, held — a cream outline in
the same frame.

**knobs**: 4 encoders right on the body, a label above each (by default "KNOB 1…4"; with `ui_state` — the current
page's parameter names; while a layer is held — the layer's knobs from the overlay).

**Knob response to touch** (Drambo-like): in the same frame — a soft glow around the well, the well's ring and the
cap's rim in `knobGlow`, the position pointer; after release it fades in 150 ms; every step flashes the pointer for
60 ms, and the pointer turns 15° per step (the encoder is endless, the angle is only visual). MASTER does the same.

**screen**: an almost black bezel #0B0B0C (rounded at 18/256 of the side), the 240×240 LCD inside. Scaling is smoothed
(default) or "pixelated" (an integer factor, nearest). A tap on the screen sends nothing to the firmware (the hardware
screen is not touch-sensitive).

## 4. The keyboard

- 16 white keys in the lower row: F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 (bit = chromatic index from F3).
- 11 black keys in the upper row, centred on the joint of the neighbouring white keys: F#3 G#3 A#3 · C#4 D#4 ·
  F#4 G#4 A#4 · C#5 D#5 · F#5 (gaps at B–C and E–F).
- As in the emulator: a dark tray, a side margin of 12/928 of the width, pitch = the rest / 16; the "pill" is 0.78 of
  the pitch wide and 0.425 of the tray high, rows centred at 1/4 and 3/4 of the height; fill #0C0C0D, rim #3A3A3E;
  a slot in the middle 0.14 of the pill wide, 0.4 of its width from the ends.
- Touch: a key's zone is its whole cell (white keys — the pitch column, the outer ones to the tray's edges; black
  keys — the cell around the joint), with no dead zones.
- Multitouch: every finger is its own key; the key mask = OR of all fingers. A "glide" option (glissando): a finger
  that slides onto a neighbouring key moves the press (off by default — the FM-1's keys are buttons).
- Haptic feedback on press (off/light/medium), `HapticFeedbackConstants.KEYBOARD_TAP` without delay.
- Key LEDs are the slot's colour, as in the emulator: off #3A3A3C, dim #77777B, mid (the drum grid) #9A9A9E,
  breath — up to #C8C8CA, lit #ECECEE with a glow; held — a cream rim in the same frame.
- **Note names on the keys**: above each key's slot — its note (F, F♯, G…; the key's bit = semitones from F3). The
  firmware's screen names keys by notes ("F SOLO", "C ALL"), so the letter lights up together with the key's LED:
  dim/mid (the key is offered as a function) — bright white, lit and held — cream, breath — breathing, off — barely
  visible. Separate function labels on the keys are not needed.
- Hints (KEY-OVERLAY.md) are drawn **inside** the pill, above the slot: 1–2 lines, 8–11 sp, with a short form.

## 5. Knob gestures

- Dragging on both axes (up = clockwise, right = clockwise; `d = −dy + dx`), so near the screen edge you can turn
  sideways (`input/KnobDrag.kt`, the remainder accumulates between events); 1 encoder step per `sensitivity` dp
  (6 dp by default; Settings → Skin); acceleration comes from the firmware itself (it has KNOB ACCEL), so we send raw
  steps.
- MASTER is absolute: 270° of travel ↔ 0…1023; the value is kept in the settings and applied before `boot()`.
- A double tap on an encoder does nothing (the hardware has no push); a long tap is "fine mode" (×0.25) while the
  finger is on the knob.

## 6. Palettes and style

`palettes.json`: `graphite` (default) — exactly the emulator's tokens (`:root` in `web/emu/index.html`): body
#1D1D1F, trays #141415, parts #2A2A2D, rims #3A3A3E, labels #D6D6D8, cream LED #FFF4D6, dim #3A3934, green #5FE08A,
red #FF453A, keys #0C0C0D. Variants: `slate` (a cold blue-grey) and `sand` (warm), built the same way (trays darker
than the body). Every palette defines all tokens itself (host test); the built-in `GRAPHITE` equals the file (host
test). The palette is part of the skin, but the user can override it.

Block frames are off by default in every skin; the "Block frames" setting draws a thin outline just outside each block.

## 7. Tests

- Host: the layout of every skin at 914×411 and 820×360 dp — symmetric margins (left == right ±1 px) with the cutout
  on the left and on the right, no overlapping controls, the settings dot outside every block and its target touching
  no knob/button/LCD/key, the system block not shortened (top = top of the area), OCT inside its island, buttons
  inside the tray, a square screen with the LCD no smaller than 602 px on the Samsung and 402 px on the POCO, every
  target ≥ 40 dp (except the noted OCT±), the keyboard = 27 keys across the full width with a `2 * u` margin.
- Device: screenshots of every skin on real phones (`adb exec-out screencap -p`).
