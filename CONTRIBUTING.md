# Contributing to FM1 DECK

Thanks for wanting to help. Audio stability, faithful firmware behaviour, fast touch play and easy firmware updates
outrank feature count — please keep that order in mind.

## Before a change

1. Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/ROADMAP.md](docs/ROADMAP.md). For the firmware side,
   read the upstream sources in `upstream/<id>/` (read-only) and [docs/SOURCE-ANALYSIS.md](docs/SOURCE-ANALYSIS.md).
2. Read the code you touch; never guess its structure.
3. Application code: verify with `./gradlew test` (report the test count you saw) and `./gradlew assembleDebug`.
   Docs/config-only changes: check paths, links and JSON; no build needed.

## Layout

| Layer | Location | Language |
|---|---|---|
| UI: panel canvas, skins, keyboard, settings | `app/src/main/java/io/github/fm1deck/ui/**` | Kotlin / Compose |
| State, settings, firmware catalog, flash | `app/src/main/java/io/github/fm1deck/{fw,settings,engine}/**` | Kotlin |
| JNI (control rate only) | `app/src/main/cpp/jni/` + `engine/NativeBridge.kt` | C++17 ↔ Kotlin |
| Audio engine (Oboe), firmware host/slot, SRC, Link | `app/src/main/cpp/{engine,fw}/` | C++17 |
| DSP: master bus, oversampling, meters | `app/src/main/cpp/dsp/` | C++17 |
| Firmware modules (GPL, one .so per firmware) | `fwbuild/` (+ `upstream/<id>` submodules) | C (gnu99) |
| Tools: fwgen, overlay extraction, golden render, icon | `tools/` | Python 3 (stdlib first) |

## Core rules

- **Audio thread:** never allocate, free, lock a mutex, wait/spin on another thread, log, do file/network I/O, call
  JNI or throw inside the Oboe callback or anything it calls. Prepare memory on control threads. Publish to the UI
  with atomics / seqlocks / triple buffers. Measure the budget from `numFrames / sampleRate`.
- **Firmware sources are never edited.** `upstream/<id>/` is read-only (git submodules). Adaptation goes into
  `fwbuild/glue`, `fwbuild/profiles`, `fwbuild/hal`. The firmware's internal sample rate is **44 100 Hz, fixed**
  (fixed-point constants are baked for it) — quality options act on SRC and the master bus, never on the firmware.
- **ABI discipline:** `app/src/main/cpp/fw/Fm1FirmwareApi.h` is a contract. Append-only within an ABI; any
  move/meaning change bumps `FM1DECK_FW_ABI` and needs an ADR in `docs/adr/`. The same goes for the skin, overlay,
  settings and package-manifest schemas: versioned, backward-compatible loading with defaults.
- **Input fidelity:** button/key changes reach the firmware with the emulator's debounce semantics (each change held
  ≥ 24 ms of device time, `HOLD_MS` in upstream `worklet.js`); a fast tap must never be lost.
- **Touch:** the whole panel (blocks + keyboard) is one canvas with one multitouch owner and geometric hit-testing
  from the skin. Holding a button with one finger while playing keys with another must always work.
- Smooth continuous parameters; switch discrete modes at block boundaries with a short crossfade.
- No global `-ffast-math`. NEON and scalar paths must agree within a stated tolerance (incl. denormals, NaN/Inf).
- **Licensing:** the app is GPL-3.0-only. Credit every port in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
  Never use M-VAVE branding, logos or trade dress; the panel is drawn in its own style.
- **Network only on a user action** (Link, a future update check). No analytics, no background traffic.

## Commands

Run Gradle sequentially — one Gradle process at a time.

```bash
python3 tools/fwgen/fwgen.py all                              # once: the firmwares' generated headers (WSL on Windows)
./gradlew assembleDebug                                       # arm64-v8a + x86_64
./gradlew assembleDebug -Pfm1deckAbis=arm64-v8a               # quick phone build
./gradlew test                                                # host unit tests (fm1HostUnitTest)
adb -s <serial> install -r app/build/outputs/apk/debug/app-debug.apk
adb -s <serial> shell am start -n io.github.fm1deck/.MainActivity
```

`test` runs the custom `fm1HostUnitTest` (JUnitCore over every compiled `*Test` class); AGP's `testDebugUnitTest` is
disabled on purpose. Native tests: [`native-tests/`](native-tests) (CMake).

## Revision marker

`APP_EDIT_REVISION` in `app/src/main/java/io/github/fm1deck/Constants.kt` is shown in Settings → About. Increase it by
+0.01 on every application code change so a deployed APK is recognisable.

## Pull requests

Say what changed and why, the commands you ran and what you observed, what you checked on a device (which phone,
which Android) and what you did not. Changes that touch the audio path should include callback timings or xrun counts.
