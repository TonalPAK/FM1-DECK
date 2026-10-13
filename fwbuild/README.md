# fwbuild — building the FM-1 firmwares for the phone

This folder builds the **firmware modules** `libfw_<id>.so` from the authors' unmodified sources
(`upstream/<id>` — git submodules) against the [`Fm1FirmwareApi.h`](../app/src/main/cpp/fw/Fm1FirmwareApi.h) contract.
Details: [docs/FIRMWARE-ABI.md](../docs/FIRMWARE-ABI.md); packaging into `.mwfw`: [docs/FIRMWARE-PACKAGES.md](../docs/FIRMWARE-PACKAGES.md).

```
fwbuild/
  CMakeLists.txt          fw_<id> targets (one translation unit per firmware)
  glue/fm1deck_module.c   HAL → profile → glue in one TU
  glue/fm1deck_glue.h     the Fm1FirmwareApi table on top of the emulator's web_* exports
  profiles/<id>.h         id, name, which capabilities are on, ui_state / save_now for that firmware
  hal/sloop/sloop_web.c   HAL for SLOOP (SLOOP has no browser emulator of its own; GPL-3.0)
  hal/hortator/hortator_web.c  HAL for Hortator: its web simulator core (tests/sim_core.c) behind the web_* surface
  overlays/<id>/          key hints (overlay.json), packed into .mwfw
```

## How it works

1. The emulator HAL (`web/emu/felucca_web.c` for Felucca/JIANT, our `hal/sloop/sloop_web.c` for SLOOP, our
   `hal/hortator/hortator_web.c` around Hortator's `tests/sim_core.c`) `#include`s
   all the firmware sources itself and models the FM-1 hardware: clock, I2S half-buffers, button matrix, LEDs,
   the 240×240 screen, 1 MB SPI NOR.
2. `profiles/<id>.h` describes the firmware and switches capabilities on and off (`FM1DECK_GLUE_*`).
3. `glue/fm1deck_glue.h` builds the `Fm1FirmwareApi` table. The core uses **only** the emulator's public `web_*`
   exports — they are stable between versions. Anything that reaches into firmware internals (host clock, save_now,
   ui_state) is optional.

## Generated headers (`gen/`)

The authors' repositories do not contain `build/gen/*.h` (fonts, icons, tables, samples) — each upstream generates
them with its own Python script (`tools/build.py` → `generate()`; Pillow + numpy + fonttools, the fonts need libraqm).
`tools/fwgen/fwgen.py <id>` runs that step into `build/fwgen/<id>/gen`.
On Windows, WSL Ubuntu is the most reliable place to run it, or CI (ubuntu-24.04, like JIANT's `emulator.yml`).

> Do not create `fwbuild/tests` or `fwbuild/firmware`: `sloop_web.c` includes `../../tests/hostsim.c` and
> `../../firmware/src/*.c` relative to itself, falling back to `-I<upstream>/sloop/firmware/src`.

## Verified

Each profile + glue (every capability, including `ui_state`) builds with NDK 28.2 (`aarch64-linux-android29`,
`-O2 -ffp-contract=off`, 16 KB pages) into `libfw_<id>.so` (≈ 0.94 MB for Felucca) exporting `fm1deck_firmware_api`:
Felucca 1.6, JIANT FM 0.6.75, SLOOP 2.6, Hortator DRUM-0.16.0.

Hortator: `tools/fwgen/fwgen.py` also cuts `sim_keymap.h` (FM1_KEYMAP) out of its `firmware/hal/fm1_input.h`, as its
`tools/build_sim.sh` does; the version is `DRUM-` + its `VERSION.txt`, as its `tools/build.py` passes it. Its CC0
instrument samples are not in its git tree, so its SAMPLE engine has the generated drum kit only (as its own
simulator built from git).
