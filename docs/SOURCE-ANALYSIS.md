# Source analysis (2026-10-12)

What FM1 DECK is built on and why that shapes its architecture. Every conclusion was checked against the code;
references point to files in `upstream/<id>/` (fresh clones from GitHub).

## 1. Four firmwares

| | Felucca | JIANT FM | SLOOP | Hortator |
|---|---|---|---|---|
| Repository | [hugelton/Felucca](https://github.com/hugelton/Felucca) | [juanjiant-bit/JIANT](https://github.com/juanjiant-bit/JIANT) | [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) | [DeadActive/hortator](https://github.com/DeadActive/hortator) |
| Author | Leo Kuroshita (Hügelton Instruments) | juanjiant-bit (a fork of Felucca 1.1.5.1 + SLOOP's song mode) | isod89 (based on Felucca) | DEADACTIVE (a drum machine based on Felucca) |
| Version / commit | v1.6, `1727537` (2026-10-13) | **v0.6.75**, `ab87ea3` (2026-10-13) | SLOOP 2.6, `8fad1a1` (2026-10-11) | DRUM-0.16.0, `7e34159` (2026-10-12) |
| Update pace | every few days | **several versions a day** (0.6.11 → 0.6.41 in one day) | the firmware is stable, the web editor changes | a release every few days |
| Browser emulator | yes (`web/emu/felucca_web.c`) | yes (the same HAL), published by CI on GitHub Pages | **no** (only `sloop-preview.wasm` — sound for the web editor) | its own simulator (`tests/sim_core.c`, `sim_*` API, not `web_*`): `fwbuild/hal/hortator/hortator_web.c` wraps it |
| Licence | GPL-3.0-only | GPL-3.0-only | GPL-3.0-only | GPL-3.0-only |
| Cheat sheet | README "Controls", `docs/controls.jpg` | `tools/gen_cheatsheets.py` (**data in Python literals**) | `GUIDE.md` §5, §9, §16 | README "On the panel" |

Conclusion: JIANT updates so often that easy updating is not an option but the core of the product. An update must
be built automatically (CI) and installed on the phone in one action.

## 2. What a "firmware" means on a phone

The real FM-1 runs on a **JieLi AC79 (WL82, pi32v2 core)**. The `.fwsc` update file is machine code for that chip
plus JieLi SDK blobs. **It cannot run on a phone**, and emulating pi32v2 makes no sense.

Felucca, though, has an excellent path: **the browser emulator**. `web/emu/felucca_web.c` is a HAL that
`#include`s *all the firmware sources unmodified* and replaces the hardware:

- the 24 MHz TIMER4 clock and `fm1_ms` advance **exactly as much as the sound asked for** (`web_render(n)` spins
  `web_ms()` until the ring holds n frames); every 128 frames (an I2S half-buffer) the real audio interrupt
  `fm1_alnk0_irq()` is called, every 15 ms the main loop (`ui_input`, `ui_draw`, `autosave_poll`, ...);
- buttons/keys/encoders by their printed names (`web_buttons(mask)`, `web_keys(mask)`, `web_enc(role, n)`,
  `web_master(adc)`), MIDI through the USB-MIDI ring (`web_midi`);
- the screen — `lcd_fill/lcd_blit` into a 240×240 RGB565 buffer (`web_screen`, counter `web_screen_draws`);
- LEDs are read from the matrix (`web_lit_*`, `web_dim_*`, `web_breath_*`, `web_mid_keys`, `web_anim_levels`);
- the 1 MB SPI NOR — a `nor[]` array with `st_read/st_erase/st_prog` hooks (`web_nor`, `web_flash_writes_count`).

Emscripten builds this into a **standalone `.wasm` with no imports at all** (`WebAssembly.instantiate(wasm, {})` in
`worklet.js`). The set of ~30 `web_*` exports is the same in Felucca and JIANT (checked by comparison), and an
earlier port repeated it for SLOOP. **This is the de-facto stable firmware ABI** FM1 DECK is built on:

- **native path**: the same `felucca_web.c` + our glue is compiled by the NDK into `libfw_<id>.so`;
- **WASM path**: the same `.wasm` the authors publish for the browser, run by the WAMR runtime inside the app.

### Verification (done)

The new `Fm1FirmwareApi.h` contract + glue + the Felucca profile **compiled with NDK 28.2 for arm64** on top of the
fresh upstream Felucca 1.5.1: `libfw_felucca.so` ≈ 0.94 MB, the `fm1deck_firmware_api` export is present, including
reading pages/layers (`ui_state`).

## 3. The firmware's sample rate is fixed at 44 100 Hz

`FS` is set in the generated tables (`build/gen/felucca_tables.h`, in SLOOP — `core.h`), and the firmware is written
in fixed point with **constants computed for 44 100 Hz**, for example:

- `eng_formant.c:73` — `sine_i(f16 * 3044u)` /* sin(pi F / FS), Q15 */; `:74` — `bw16 * 4781u` /* pi BW / FS */;
- `eng_noise.c:148` — `(r * 97391u) >> 16` /* rate * 65536 / FS */;
- `eng_lofi.c:362` — `FRAME_Q16` from `FS`; `click.c` — `k = 2 sin(pi f / FS)`;
- the real I2S on the hardware is ≈ 44 117.6 Hz (`DAC_TICKS 544` from 24 MHz); the emulator runs at exactly 44 100.

**The firmware's internal rate cannot be raised with a flag** — pitches, resonances and tempos would change, and the
grit and aliasing are part of the device's character. So "48–96 kHz and ×2…×8" are done this way (ADR-0003):
firmware at 44.1 kHz → a good SRC → engine at 48/96 kHz → the master bus, whose non-linear stages (saturation,
clippers, the true-peak detector) run oversampled ×1/×2/×4/×8. That gives weight without spoiling the original sound.

## 4. The firmware's UI state is reachable

The same translation unit exposes (Felucca 1.6 / JIANT 0.6.75 / SLOOP 2.6 / Hortator 0.16.0):

- `ui.page` — an index into `PAGES[]`, `PAGES[i].title` ("DLY 2"), `page_desc(pg, k, &val)` → `param_desc_t
  {label, min, max, ...}` and a pointer to the current value;
- `ui.layer` — the open "layer map" (`LAYER_NONE/FX/GLO/SCL/EDIT/SEQ/REC` in Felucca, + `SAVE/MACRO` in JIANT;
  SLOOP has `LY_PLAY/FX/ERASE/ROLL/STEP/SCALE/MIX/SONG`; Hortator has no layers, only `ui.layer` = PERFORM shown),
  `ui.home` (Hortator's HOME knobs: `home_param()`).

So the labels of the four knobs and the current layer can come **straight from the firmware** (the
`FM1_CAP_UI_STATE` capability): hints update together with the firmware, and a value ring can be drawn around the
virtual encoders. A double press on a layer button latches it without holding — impossible to track from presses,
easy from `ui.layer`.

## 5. Behaviour to reproduce exactly

- **Debounce/hold** (`worklet.js`, `HOLD_MS = 24`): every change of the button or key mask is held ≥ 24 ms of
  *device time* before the next one is applied. Otherwise a fast tap (down+up within one block) is lost.
  Applying events in a batch at the start of a block is not enough — FM1 DECK gates them.
- **Page buttons fire on release**; holding opens a layer; a double tap latches the layer.
- **RESTORE LAST**: the firmware writes its autosave only when the transport is stopped, nothing was touched for
  10 s, and at most once a minute. When the app goes to the background, `save_now` is needed (it is in the native
  glue), otherwise up to a minute of work is lost.
- **The flash format changes between versions** (Felucca README: "⚠ Coming from 1.1.5"): before installing a new
  version — an automatic flash backup; rollback restores both the firmware and the flash.
- **`web_seed`** (JIANT 0.4+): session macros depend on the random seed before `web_boot`.
- **`web_test_heavy`** (Felucca/JIANT): a heavy test song (FM6 PAD, 4-note chords, DRUM) — a ready-made load for
  performance measurements.

## 6. Generated headers

`build/gen/*.h` (tables, fonts, icons, keys, palettes, FM6 patches, samples) is **not stored** in the repositories.
Each upstream's Python step `tools/build.py → generate()` makes them (Pillow, numpy, fonttools; the font rasteriser
needs libraqm — easier through WSL on Windows; JIANT's CI does it on ubuntu-24.04). FM1 DECK runs that step with
`tools/fwgen/`.

## 7. Where FM1 DECK's host code comes from

The firmware host was ported from an earlier, proven port of these firmwares by the same author (credited in
[THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md)) and reworked:

| Earlier port | In FM1 DECK | What changed |
|---|---|---|
| module API header | `app/src/main/cpp/fw/Fm1FirmwareApi.h` | `struct_size`, `caps`, `id`, `build_info`, `seed`, `ui_state`, `test_heavy`; optional fields |
| glue header | `fwbuild/glue/fm1deck_glue.h` | the core uses only `web_*`; internals are optional capabilities with a CI fallback |
| CMake | `fwbuild/CMakeLists.txt` | HAL/profile/version per firmware, 16 KB pages, capability flags from CI |
| SLOOP host HAL | `fwbuild/hal/sloop/sloop_web.c` | unchanged |
| service | `fw/FirmwareHost` | `.so` path from the installed package (not a fixed name), WASM backend, probe process |
| track slot | `fw/FirmwareSlot` + `dsp/Resampler` | no tracks/transport; + HOLD_MS gating; + `ui_state` seqlock; SRC 32/64 taps NEON |
| JNI bridge | `jni/JniFirmware.cpp` | the screen goes straight into a `Bitmap` (`AndroidBitmap_lockPixels`), no int[] copies |
| panel page | `ui/panel/*` | not ported directly: key geometry/LED colours as a model; the panel is one Canvas from the skin |
| Oboe adapter, profiler, fast math | `engine/`, `dsp/` | as a model for the Oboe configuration and the profiler |
| MediaTek notes | `docs/AUDIO-ENGINE.md` §MediaTek | pinning the callback to the A75 cores (SCHED_FIFO → EPERM) |

## 8. Licences

Everything that runs the firmwares is GPL-3.0-only, so the whole app is GPL-3.0-only (ADR-0002). Firmware packages
carry the upstream `LICENSE`, `LICENSING.md`, `LICENSES/` (OFL fonts, msfa Apache-2.0, Fukiai MIT) and the exact
source commit. JieLi SDK blobs are needed only for the real device's `.fwsc` — they never get into our build.
No M-VAVE branding, logos or enclosure look: the panel is drawn in its own style.
