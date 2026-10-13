# How FM1 DECK works

An overview for developers. The detailed design documents (in Russian) are listed in [README.md](README.md).

## The idea

The FM-1 firmwares are plain C for an RP2040-class microcontroller, and each project already ships a **browser
emulator HAL** (`web/emu/felucca_web.c` in Felucca and JIANT) that models the hardware: the millisecond clock, the
I²S half-buffers, the button matrix, the LEDs, the 240×240 RGB565 LCD and the 1 MB SPI NOR flash. The emulator
compiles that HAL plus all firmware sources into WebAssembly. FM1 DECK compiles the **same** files with the Android
NDK into a native module, so the phone runs the authors' code unmodified.

## Layers

| Layer | Where | What |
|---|---|---|
| Firmware module | `fwbuild/` → `libfw_<id>.so` | HAL + firmware sources + `glue/fm1deck_glue.h`, which exposes the C table `Fm1FirmwareApi` (ABI 1): `boot`, `render`, `buttons`, `keys`, `enc`, `master`, `midi`, `host_clock`, `screen`, `leds`, `nor`, `save_now`… SLOOP has no browser HAL, so `fwbuild/hal/sloop/sloop_web.c` provides one. |
| Firmware host | `app/src/main/cpp/fw/` | `FirmwareSlot` drives one module from the audio thread in 64-frame sub-blocks: inputs in order with the hardware's 24 ms debounce gating, lock-free queues, LED atomics, a triple-buffered screen, park/unpark for switching firmware. |
| Audio engine | `app/src/main/cpp/engine/` | Oboe (AAudio), float, exclusive low-latency when available. The firmware renders 44.1 kHz; a band-limited resampler converts to the stream rate. |
| Master bus | `app/src/main/cpp/dsp/` | Saturation → spectral clipper → look-ahead true-peak limiter → final clipper, own half-band FIR oversampling ×1–×8, residual ADAA, LUFS / true-peak meters. Parameters arrive through an SPSC queue; nothing allocates or locks in the callback. |
| Link | `engine/LinkSync`, `engine/ClockPulser` | Ableton Link session → 24 PPQN MIDI clock (F8), Start (FA) on beat 0, Stop (FC), timed per sub-block as heard (output + master-bus latency). |
| UI | `app/src/main/java/` | Kotlin, Jetpack Compose. The panel is one `Canvas` laid out from a skin JSON; touches become button / key masks and encoder steps over JNI. |

## Real-time rules

Everything reachable from `onAudioReady` is allocation-free and lock-free: SPSC queues and atomics only, no logging,
denormals flushed, no exceptions. Control-thread work (loading a firmware, presets, Link enable) happens outside.

## Data

Each firmware's 1 MB flash image lives in the app's private storage and is saved on switch and periodically.
Settings are one JSON file; master presets are JSON (`assets/presets/master/` and the user's own).

## Tests

- Host (JUnit): skin layout on both test phones' geometries, palettes, settings round-trip, master parameter table.
- Native (`native-tests/`, clang, ASan/UBSan): firmware slot gating and park stress, resampler, master bus (null
  test, ceiling never exceeded, aliasing vs quality, clicks, latency), Link clock pulses.
- Devices: Samsung Galaxy M52 5G, POCO C75.
