# Testing and verification

## Levels

| Level | Where | What |
|---|---|---|
| Host Kotlin | `app/src/test`, `gradlew test` (`fm1HostUnitTest`) | skin layout, HintEngine, manifest parsing/validation, package installer (zip-slip, hashes, signature), settings migrations, master-bus parameters from the table |
| Native host | `native-tests/` (CMake, clang on Windows or WSL) | FirmwareSlot with a fake firmware (HOLD_MS gating, park/unpark, screen triple buffer), Resampler (response/aliasing/latency), MasterBus (all MASTER-BUS §8 tests), Ed25519/SHA-256 vectors |
| Firmware golden | `tools/render/` + Node | native module (x86_64 host build) vs upstream WASM: the same input script → the same float32 output |
| Device | real phones (tested: Samsung Galaxy M52 5G, POCO C75) | start, sound, the multitouch matrix, skin screenshots, callback timing (Perfetto), package install from `filesDir` |
| CI | GitHub Actions | `assembleDebug`, `test`, native tests, package builds + golden |

## Input scripts (`tools/render/scripts/*.json`)

```json
{ "firmware": "jiant", "seconds": 12, "seed": 1, "master": 700,
  "events": [ { "ms": 1500, "buttons": ["PLAY"] }, { "ms": 1560, "buttons": [] },
              { "ms": 2000, "keys": ["C4","E4","G4"] }, { "ms": 2400, "keys": [] },
              { "ms": 3000, "enc": { "K1": 5 } }, { "ms": 4000, "call": "test_heavy" } ] }
```
Times are device milliseconds (`now_ms`), so a script replays identically in native and WASM.

## Multitouch matrix (on a device, by hand; some phones block injected touches)

Hold FX + play 3 keys; GLO + SELECT; REC + FX (a combo); hold a step + turn a KNOB; 5 fingers on the keys; fast taps
on PLAY (10 per second — none may be lost); an edge swipe (system gesture) while holding — every button is released
correctly (cancel).

## Performance gates

See AUDIO-ENGINE §5 and MASTER-BUS §7: callback p99, xrun = 0 over 3 minutes of a heavy song, measured on real phones
with the configuration and thermal state stated.

## Reporting

A change that touches audio or the panel should state the commands run, the observed results (test counts, sizes,
timings), what was checked on a device and what was not, and open questions.
