# Firmware ABI — how the app runs firmwares

Contract: [`app/src/main/cpp/fw/Fm1FirmwareApi.h`](../app/src/main/cpp/fw/Fm1FirmwareApi.h) (ABI 1).
The "native + WASM" decision: [ADR-0001](adr/0001-firmware-execution.md). Today the app ships the native backend;
the WASM backend is planned (P7).

## 1. Two backends, one interface

```cpp
// app/src/main/cpp/fw/IFirmware.h
struct IFirmware {
    virtual ~IFirmware() = default;
    virtual uint32_t caps() const = 0;            // FM1_CAP_*
    virtual const FirmwareInfo& info() const = 0;  // id, name, version, build_info, sample_rate, nor_size, button names
    virtual uint8_t* nor() = 0;  virtual uint32_t norWrites() = 0;  virtual void norErase() = 0;
    virtual void seed(uint32_t) {}                 // only with FM1_CAP_SEED
    virtual void boot() = 0;
    virtual void render(uint32_t n, float* l, float* r) = 0;   // n <= 1024, 44.1 kHz
    virtual void buttons(uint32_t) = 0; virtual void keys(uint32_t) = 0;
    virtual void enc(uint32_t role, int32_t steps) = 0; virtual void master(uint32_t adc) = 0;
    virtual uint32_t midi(uint32_t s, uint32_t d1, uint32_t d2) = 0;
    virtual void hostClock(uint32_t) {}  virtual int32_t saveNow() { return -1; }
    virtual const uint16_t* screen() = 0; virtual uint32_t screenDraws() = 0;
    virtual void leds(uint32_t out[FM1_LED_COUNT]) = 0; virtual const uint8_t* animLevels() = 0;
    virtual uint32_t playing() = 0; virtual uint32_t nowMs() = 0;
    virtual bool uiState(Fm1UiState*) { return false; }
    virtual void testHeavy() {}
};
```

| | NativeFirmware | WasmFirmware |
|---|---|---|
| Artifact | `libfw_<id>.so` (our CI / built into the APK) | `firmware.wasm` (upstream `felucca.wasm` or our build) + optional `firmware-<abi>.aot` |
| Loading | `dlopen(path, RTLD_NOW|RTLD_LOCAL)` → `dlsym("fm1deck_firmware_api")` → check `abi`, `struct_size` | WAMR: `wasm_runtime_load` → instantiate (1 MB stack, heap 0; 32 MB module memory) → `_initialize` |
| "Power cycle" | `dlclose` + `dlopen` (static memory from scratch) | a new module instance |
| Speed | native | AOT ≈ native; the fast interpreter is several times slower (measured in P7, a gate) |
| Capabilities | everything in the profile (`host_clock`, `save_now`, `ui_state`, `seed`, `test_heavy`) | only what the `.wasm` exports: `seed` (JIANT), `test_heavy`; **no** `save_now`, `host_clock`, `ui_state` |
| Risk | native code with the app's permissions → signing is mandatory | sandbox: the module's memory is isolated |

## 2. Mapping to the emulator's exports

| Fm1FirmwareApi | web_* export (WASM and native HAL) | Note |
|---|---|---|
| `nor()` / `nor_writes()` / `nor_erase()` | `web_nor`, `web_flash_writes_count`, `web_nor_erase`, `web_nor_size` | WASM: a pointer into linear memory |
| `seed(s)` | `web_seed` | JIANT ≥ 0.4 only, before `boot()` |
| `boot()` | `web_boot` | once per "power on" |
| `render(n, l, r)` | `web_render(n)` + `web_out_l()`, `web_out_r()` | WASM: a copy from linear memory |
| `buttons/keys/enc/master` | `web_buttons`, `web_keys`, `web_enc`, `web_master` | |
| `midi()` | `web_midi` (channel messages) | real-time F8/FA/FB/FC — native glue only (`midi_enqueue`) |
| `screen()/screen_draws()` | `web_screen`, `web_screen_draws` | RGB565 big-endian (high byte first) |
| `leds()` | `web_lit_buttons`, `web_lit_keys`, `web_dim_buttons`, `web_dim_keys`, `web_breath_buttons`, `web_breath_keys`, `web_mid_keys`, `web_dim_level` | `web_mid_keys` may be missing in old wasm → 0 |
| `anim_levels()` | `web_anim_levels` | 41 bytes or 0 |
| `playing()/now_ms()` | `web_playing`, `web_now_ms` | |
| `test_heavy()` | `web_test_heavy` | Felucca/JIANT |
| — | `web_sample_rate` | check: must be 44100 |

On load, `WasmFirmware` checks that the **mandatory** exports exist (everything except `web_seed`, `web_mid_keys`,
`web_anim_levels`, `web_test_*`) and refuses with a clear error if something is missing.

## 3. Calling rules (shared by both backends)

1. One thread inside the module at a time (audio while live; control only after `park()`).
2. Power-on order: `nor_erase()` → (if there is a saved image) copy into `nor()` → `seed()` (if any) →
   `master(adc)` → `boot()`.
3. `render` in blocks ≤ 1024; the slot's inner block is 64 frames at the SRC output, input frames — as many as
   `Resampler.inputNeeded()` asks for.
4. Button/key input is gated with HOLD_MS = 24 ms of device time (see ARCHITECTURE §3).
5. `screen()` is read only when `screen_draws()` changed and the UI wants the screen; a 115 200-byte copy.
6. `ui_state()` at most once per ~16 ms; the result is published through a seqlock structure.
7. No call may allocate or block — guaranteed by the firmware sources (MCU code with no malloc in the audio path)
   and by our glue. The WASM runtime must not allocate after instantiate either (to verify in P7: WAMR with a
   pre-allocated pool, `os_malloc` forbidden on the hot path).

## 4. Determinism and the golden test

Native and WASM are built with `-ffp-contract=off -O2` from the same sources → for the same input script the output
must match bit for bit (the SLOOP editor claims the same for its preview). The golden test: the script
`tools/render/scripts/<id>-smoke.json` (device-time ms → buttons/keys/encoders), a 10 s render with the native module
and the upstream wasm in Node → float32 comparison. This test is a gate for any new package.

## 5. Versioning

- `FM1DECK_FW_ABI` grows only on an incompatible change of the table. Appending fields + growing `struct_size` is
  compatible; the host reads a field only if it fits in `struct_size`.
- The package manifest carries `abi` (native), `wasmExports` (a list), `capabilities` (what was actually built).
- About shows the ABI, the firmware version, `build_info` and the backend.

## 6. Firmware profiles (`fwbuild/profiles/<id>.h`)

| Capability | Felucca | JIANT | SLOOP |
|---|---|---|---|
| `HOST_CLOCK` (`G_CLOCK`/`G_SYNC` + `midi_enqueue`) | yes | yes | yes (`G_SYNC`) |
| `SAVE_NOW` | yes (Felucca autosave) | yes | yes (its own `persist_flush_now`) |
| `UI_STATE` | yes (verified by compiling) | yes | P4 (its own `LY_*`, `PAGES`) |
| `SEED` | — | yes | — |
| `TEST_HEAVY` | yes | yes | P5 (its own test song) |

If an upstream update breaks the build of an optional capability, CI rebuilds with
`-DFM1DECK_GLUE_FLAGS_<id>="FM1DECK_GLUE_<CAP>=0"` and the app degrades gracefully (ARCHITECTURE §9).
