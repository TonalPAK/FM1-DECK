# The settings menu

Settings open from a small dot in a free corner of the panel (above the screen block, outside the touch zones; the
position comes from the skin: `"gear": "top-right"`). It is a full-screen menu (Compose Material3, dark) with tabs on
the left. Changes apply at once; sound is not interrupted, except when the stream rate/mode changes (a short fade and
a reopen).

Items marked (Pn) are planned for that phase.

## Tabs

### 1. Skin
- Six layout thumbnails (the real geometry drawn small) + skin import (JSON).
- Palette; block frames (as in the skin / all / none); keyboard height 30–45 %; mirroring (screen ↔ system) — P2+.
- Firmware screen: smoothed / pixelated.
- Knobs: sensitivity (3–12 dp per step), fine mode on long tap on/off.
- Keys: glissando on/off; haptic feedback off/light/medium.
- Hints (KEY-OVERLAY.md §5).

### 2. Firmware
- The active firmware: Felucca / JIANT / SLOOP (switching: save the current one's flash → load the chosen one;
  ≈ 0.3 s of silence with a fade).
- For each: version, source (commit), backend (native/wasm), capabilities, "Cheat sheet", "What's new" (from
  SOURCE.md) (P3).
- Install from file · Check for updates (release/nightly channel) · Wi-Fi from PC (P8) · History and rollback (P3).
- Flash: back up now · restore · export/import (`.nor`, the browser emulator's format — P9) · erase (confirmed by
  typing the firmware's name) (P3).
- Developer: allow unsigned packages (red warning); prefer WASM; load log (P3).

### 3. Audio
- Stream rate: Auto / 48 000 / 96 000. "What we got": actual rate, burst, buffer, AAudio/OpenSL, exclusive/shared,
  output latency, a "tap → sound" estimate.
- Buffer: Minimum / Safe / manual (1–8 bursts).
- Render mode: Direct / Buffered (+N × 2.9 ms).
- **Quality ×1 / ×2 / ×4 / ×8 / Auto** (explained as "master-bus oversampling; the firmware always runs at 44.1 kHz").
- SRC: Standard / HQ.
- CPU: callback load p50/p99, xrun counter (reset), pinning to the big cores (Auto/On/Off), ADPF on/off.
- Play in the background (P8): off / while playing / always.

### 4. Master
- A chain of 4 stages (cards with on/off and the parameters of `MasterBusParams.h`), Bypass, Mix, Input/Output.
- Meters: input/output peak + RMS, GR of each stage, true peak, LUFS-S; the saturator's transfer curve (an
  input/output plot with the current point).
- Presets (built-in + your own), A/B, Auto-gain for a fair comparison.
- Layout: one page, no scrolling strip. On top: Bypass, Auto-gain, A/B, the preset menu (with "save as"), the SCOPE
  switch. Left: a scope of the last ~2 s (input as a blue outline, output as a cream fill, reduction in red from the
  top, the ceiling dashed; the scale is the square root of amplitude with +6 dB headroom) and meters. Right: the chain
  I/O › SAT › SPEC › LIMIT › CLIP (a dot = the stage is on, a bar = its current work); the chosen stage opens below,
  controls two per row, thin faders (drag anywhere, double tap = default). The scope can be switched off (setting
  `masterScope`); outside the tab the bus records no history.

### 5. MIDI
- Ableton Link ([LINK.md](LINK.md)).
- USB/BLE devices (connect/disconnect), routing: channel → firmware (`web_midi`), clock: internal / external
  (`host_clock`, native packages only); MIDI learn is done by the firmware itself (P9).

### 6. Recording (P8)
- WAV format 24-bit / 32-float, the tap point (after the master / before the master), folder (SAF), "Share".

### 7. About
- App version, `APP_EDIT_REVISION`, ABI, firmware versions + `build_info`, licences (GPL-3.0 and third-party),
  links to the sources of every part (the GPL obligation), the firmware authors.

## Storage

`filesDir/settings.json`, with a `schema` field (starting at 1). Loading an unknown/old schema → defaults for the
missing fields + a migration test. Defaults live in `AppSettings.kt` (one source). The MASTER pot (0…1023) and the
last skin/firmware are stored here too.
