# Roadmap

The project moves in phases; each one has exit criteria rather than dates.

| Phase | Goal | Exit criteria (short) | Status |
|---|---|---|---|
| **P0** Bootstrap | git, submodules, the skeleton builds, CI, deploy to phones | the APK starts on both test phones and shows the native version; CI green | done |
| **P1** Firmware host | fwgen + fwbuild for 3 firmwares, FirmwareHost/Slot (+ HOLD_MS), Oboe Direct, SRC, golden test | all 3 firmwares play and respond on phones; golden native == wasm for Felucca and JIANT; p99 within AUDIO-ENGINE §5 at Q×1 | done |
| **P2** Panel & skins | one Canvas, layout engine, 6 skins, keyboard, LEDs, screen, knobs, multitouch, settings + Skin tab | skin screenshots on both phones match the references; multitouch matrix passed | done |
| **P3** Firmware manager | `.mwfw`, signing, install/rollback, flash and backups, built-in packages, "Open with", update check, CI firmware packs | a new JIANT version goes from upstream to the phone without a manual build; rollback restores the flash too; dlopen from filesDir verified | planned |
| **P4** Hints | HintEngine, overlays for JIANT (automatic) + Felucca + SLOOP (manual), ui_state for SLOOP, "Explain" mode | holding each layer of each firmware shows the right labels; validator green | planned (note names on keys are done) |
| **P5** Audio quality | 48/96 kHz, HQ SRC (NEON), oversampler ×1…×8, Buffered mode, CPU meter, core affinity, "Auto" | AUDIO-ENGINE §5 gates for every mode; no xrun for 3 min | partly done |
| **P6** Master bus | 4 stages, meters, presets, Master tab | MASTER-BUS §8 tests; §7 CPU measurements on both phones | done |
| **P7** WASM backend | WAMR (fast-interp + AOT), import of upstream `.wasm`, our wasm for SLOOP, AOT in packages | upstream `felucca.wasm` / JIANT wasm from GitHub Pages plays; interpreter vs AOT vs native measured | planned |
| **P8** Extras A | Wi-Fi upload from a PC, WAV recording, playing in the background (foreground service) | a package from a PC installs over Wi-Fi with confirmation; 10 min recording without drops | planned |
| **P9a** Ableton Link | tempo, phase and start/stop over Wi-Fi between phones and apps ([LINK.md](LINK.md)); MIDI clock into the firmware | two phones play together; start on the beat | done |
| **P9** Extras B | USB/BLE MIDI + clock; flash ⇄ browser emulator; USB exchange with a hardware FM-1 (spike) | an external keyboard plays; clock syncs; spike report | planned |
| **P10** Release | APK signing, GitHub Releases, Obtainium, licences/About, user manual | 0.1.0 installs on both test phones | 0.1.0 preview |

## Risks and what removes them

| Risk | Where it is checked | Plan B |
|---|---|---|
| The firmware's `ui_draw` inside the callback causes spikes on slower phones | P1 measurements | Buffered mode (P5), pinning to the big cores |
| `dlopen` from `filesDir` is forbidden on some Android version | P3 gate | WASM backend for the same package; built-in versions |
| Upstream renamed internals → the glue no longer builds | CI on every build | auto-rebuild without that capability, graceful degradation |
| Generating `gen/` on Windows (libraqm) | P1 | WSL Ubuntu / CI only |
| The flash format changes between versions | P3 | backup before an update, `flash.migration`, rollback |
| The WAMR interpreter is too slow on low-end phones | P7 measurement | AOT in packages, native by default |
| 96 kHz is not native on the phones | P5 | show the OS resampling honestly, 48 kHz by default |
