# FM1 DECK architecture

Parts marked (Pn) are planned for that phase ([ROADMAP.md](ROADMAP.md)).

## 1. The whole picture

```
 ┌──────────────────────────── UI thread (Kotlin / Compose) ────────────────────────────┐
 │  PanelSurface (one Canvas, one multitouch owner)                                      │
 │   ├─ SkinLayout  ← assets/skins/*.json        ├─ HintEngine ← overlay.json + ui_state │
 │   ├─ blocks: SYSTEM · BUTTONS · KNOBS · SCREEN └─ LedRenderer ← LED masks             │
 │   └─ KEYBOARD 27 keys across the full width                                           │
 │  Settings: Skin · Firmware · Audio · Master · MIDI · Recording · About                │
 └───────────────┬──────────────────────────────────────────────▲─────────────────────────┘
   masks/steps   │ NativeBridge (JNI, control rate only)        │ LEDs / screen / ui_state / meters
                 ▼                                              │ (atomics, seqlock, triple buffer)
 ┌──────────────────────────── control thread(s) (Kotlin coroutines + native) ──────────┐
 │ FirmwareManager: .mwfw packages, signature check, install, rollback, flash backups    │
 │ FirmwareHost: dlopen / WAMR, park → load/unload → unpark, save_now, flash snapshots   │
 │ AudioService: open/reopen the Oboe stream, settings, foreground service               │
 └───────────────┬──────────────────────────────────────────────────────────────────────┘
                 ▼
 ┌──────────────────────────── Oboe audio callback (C++17, real time) ──────────────────┐
 │ FirmwareSlot: event queue (HOLD_MS) → fw.render(44.1k) → publish LEDs/screen          │
 │      → Resampler 44.1k → Fe (48k/96k) → MasterBus (×Q on non-linearities) → Meters    │
 │      → [Recorder tap] → output                                                        │
 └──────────────────────────────────────────────────────────────────────────────────────┘
   Firmware backends:  NativeFirmware (libfw_<id>.so, Fm1FirmwareApi)  |  WasmFirmware (WAMR, web_* exports, P7)
```

## 2. Threads and ownership

| Thread | What it does | What it must not do |
|---|---|---|
| UI (main) | draws the panel, gestures → masks, reads the published LEDs/screen/ui_state once per frame (Choreographer) | block on native calls longer than ~1 ms |
| Control (Dispatchers.IO / a single executor) | loading/unloading firmwares, flash, packages, opening the stream | touch the firmware while the slot is not `park()`ed |
| Oboe audio callback | everything that sounds; the only caller of the firmware while the slot is live | allocations, mutexes, logs, I/O, JNI, exceptions |
| Device thread (Buffered mode, P5) | renders the firmware ahead into an SPSC ring; the callback only reads | same as audio |
| `:fwprobe` process (P3) | trial load of a new package (dlopen + 1 s of rendering) before activation | — |
| Wi-Fi drop server (P8) | receives `.mwfw` over the LAN, only while the dialog is open | install a package without confirmation on the phone |

**The firmware rule:** at any moment exactly one thread is inside the firmware module. Changing the owner is the
`park()/unpark()` handshake (Dekker): after `park()` the audio thread is guaranteed to be outside the module.

## 3. Input data flow

1. `PanelSurface` gets `PointerEvent`s, finds each pointer's target from the skin geometry (button / key / knob /
   screen) and keeps it until release.
2. `InputState` holds the button mask (14 bits), the key mask (27 bits), accumulated encoder steps (7), MASTER
   (0..1023). Any change → one call to `NativeBridge.panel(buttons, keys)` / `enc(role, steps)` / `master(v)`.
3. `FirmwareSlot` puts **only masks that changed** into an SPSC queue (like the emulator's page: `index.html` sends
   `buttons` or `keys` only when the mask changed; if both changed in one call — buttons first, then keys). An
   unchanged mask in the queue would take a 24 ms gate window and delay a note. The producer is one control/UI thread
   behind a producer-side mutex; the consumer is audio. At the start of every inner block events are applied **with
   HOLD_MS gating**: a new button/key mask is applied only if ≥ 24 ms passed since the last change by `fw.now_ms()`.
   Encoders, MASTER and MIDI go without delay.
4. The firmware renders. Every ~16 ms the audio thread publishes the LEDs (atomics), `playing`, `nor_writes`,
   `ui_state` (seqlock); the screen through a triple buffer, only if `screen_draws` changed and the UI shows it.

## 4. The audio path

`fw.render` (44 100 Hz, blocks up to 1024) → `Resampler` (polyphase windowed sinc, 32 or 64 taps, NEON) → the engine
rate **Fe** = the stream rate (48 000 by default, 96 000 optional, "Auto" = the device's native rate) → `MasterBus`
(see MASTER-BUS.md; the quality multiplier **Q ∈ {1,2,4,8}** oversamples the non-linear stages) → limit/clip →
float32 stereo output. Details: [AUDIO-ENGINE.md](AUDIO-ENGINE.md).

## 5. Code modules

```
app/src/main/java/io/github/fm1deck/
  MainActivity.kt                 lifecycle, full-screen mode, display cutouts
  Constants.kt                    APP_EDIT_REVISION, limits
  engine/NativeBridge.kt          every external function (the single JNI entry point)
  engine/AudioController.kt       start/stop/reopen the stream, audio settings
  engine/LinkController.kt        Ableton Link on/off, multicast lock
  engine/MasterControl.kt         master-bus parameters and presets
  fw/FirmwareCatalog.kt           built-in firmwares, the active one, switching
  fw/flash/{FlashStore,FlashFileStore}.kt     NOR images, saving
  ui/panel/{PanelSurface,SkinLayout,InputState,PanelState,...}.kt, ui/panel/render/*, ui/panel/skin/*
  ui/settings/{SettingsScreen,AudioTab,MasterTab,LinkTab}.kt
  settings/{AppSettings,SettingsStore}.kt     SharedPreferences + JSON, schema version
app/src/main/cpp/
  jni/        JniEngine.cpp, JniFirmware.cpp, JniLink.cpp, JniCommon.h
  engine/     AudioEngine (Oboe callback), CpuMeter, LinkSync, ClockPulser
  fw/         Fm1FirmwareApi.h (the contract), IFirmware.h, NativeFirmware, FirmwareHost, FirmwareSlot
  dsp/        Resampler, Oversampler, Crossover, Shaper, Meters, MasterBusParams.h (contract), master/* stages
fwbuild/      firmware modules (C, GPL)       tools/   fwgen, overlay, render, icon
upstream/     felucca, jiant, sloop, hortator (submodules, read-only)
```

Planned modules: `fw/pack/*` (packages, P3), `ui/hints/*` (P4), `engine/Affinity` (P5), `fw/WasmFirmware` (P7),
`engine/Recorder` (P8).

`IFirmware` (C++): the same operations as `Fm1FirmwareApi`, plus `caps()`. `NativeFirmware` wraps the table from
`dlsym`; `WasmFirmware` will wrap a WAMR instance and the `web_*` exports. `FirmwareSlot` does not know which backend
it has.

## 6. JNI (control rate)

| Group | Functions (Kotlin `NativeBridge`) |
|---|---|
| Stream | `startAudio(cfg)`, `stopAudio()`, `streamInfo(): IntArray` (rate, burst, buffer, api, exclusive), `cpuLoad()` |
| Firmware | `fwLoadNative(path, nor: ByteArray?, seed)`, `fwLoadWasm(path, aotPath?, nor, seed)` (P7), `fwUnload()`, `fwInfo(): String`, `fwCaps(): Int`, `fwSaveNow(): Int`, `fwSnapshotNor(): ByteArray?`, `fwNorWrites(): Long` |
| Panel | `panel(buttons: Int, keys: Int)`, `enc(role: Int, steps: Int)`, `master(adc: Int)`, `midi(status, d1, d2)` |
| Outputs | `leds(out: IntArray)`, `screenInto(bitmap: Bitmap, seen: Int): Int`, `uiState(out: IntArray, labels: Array<String?>): Boolean`, `setScreenWanted(Boolean)` |
| Master | `masterParamTable(): FloatArray` (from `MasterBusParams.h`), `masterSet(id, value)`, `masterMeters(out: FloatArray)` |
| Quality | `setQuality(q)`, `setSrcQuality(taps)`, `setRenderMode(mode, aheadBlocks)` |

The exact signatures live in `engine/NativeBridge.kt`.

## 7. Lifecycle

- `onStart`: open the stream (if closed), `setScreenWanted(true)`.
- `onStop`: `setScreenWanted(false)`; if the firmware is playing and "Play in the background" is on — a foreground
  service (P8) keeps the stream; otherwise `save_now` → a flash snapshot → written to disk (atomically) → close the
  stream.
- The flash is also saved when `nor_writes` grows (2 s debounce, on a control thread, the snapshot in parts between
  renders) — protection against the process being killed.
- Unplugging headphones / a route change: Oboe `onErrorAfterClose` → reopen on a control thread; the firmware is not
  restarted (its state is in the module's static memory).
- A module crash inside the process cannot be healed: a `fw_loading` marker is written before loading; if the marker
  is still there at start — "safe mode" (the built-in firmware version, an offer to roll back the flash backup).

## 8. Storage (`filesDir`)

```
fw/<id>/<version>-<sha8>/        an unpacked package (read-only): manifest.json, native/<abi>/libfw.so, wasm/, overlay/  (P3)
fw/state.json                    the active firmware, the chosen version of each, rollback history  (P3)
flash/<id>/nor.bin               the current NOR image (1 MB, written atomically: tmp + rename)
flash/<id>/backups/<ts>-<ver>.nor an automatic backup before a version change, manual backups (limit 10)  (P3)
settings.json                    settings (skin, audio, master, hints), schema N
presets/master/*.json            master-bus presets
recordings/                      WAV (P8), exported through SAF/Share
```

The built-in firmwares (built into the APK from `fwbuild/`) live in `nativeLibraryDir`; an installed package newer
than the built-in one takes priority (P3).

## 9. Errors and degradation

| Situation | Behaviour |
|---|---|
| `dlopen` from `filesDir` is forbidden on the device | the same package through the WASM backend (if it has a `.wasm`), otherwise the built-in version |
| a package fails its signature/hash | not installed; unsigned ones only with the developer switch |
| the firmware cannot keep up (xrun) | the xrun counter in Audio → suggest Buffered, a lower Q, pinning to the big cores |
| no `FM1_CAP_UI_STATE` | hints from held buttons (+ a double-tap heuristic for latching) |
| no `FM1_CAP_SAVE_NOW` (WASM) | the flash is saved by `nor_writes`; a warning "RESTORE LAST may lag by up to a minute" |
