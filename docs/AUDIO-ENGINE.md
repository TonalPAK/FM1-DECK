# Audio engine

## 1. The Oboe stream

| Parameter | Value | Why |
|---|---|---|
| API | AAudio (Oboe chooses; OpenSL ES only as a fallback) | minSdk 29 |
| PerformanceMode | `LowLatency` | playing on the keys |
| SharingMode | `Exclusive` → `Shared` if refused | the MMAP path, lower latency |
| Format / channels | `Float`, stereo | no conversions in the callback |
| Rate | Auto (the device's native rate) / 48 000 / 96 000 | see §2 |
| Oboe SRC | `SampleRateConversionQuality::None` for "Auto"; otherwise `Medium` | 96 kHz on a phone that cannot do it is resampled by the system — we show that honestly |
| Buffer | `setBufferSizeInFrames(2 × burst)` at start; "Safe" = 4 × burst; manual 1–8 bursts | latency ↔ stability |
| Usage / Content | `Media` / `Music` | |
| ADPF | `setPerformanceHintEnabled(true)` where Oboe 1.10 has it | Android 13+ raises clocks for the callback |
| Callback | `onAudioReady` → `AudioEngine::process(float* out, int n)` | |
| Errors | `onErrorAfterClose` → a flag; a control thread reopens the stream (never from the callback) | |

The actual `sampleRate`, `framesPerBurst`, `bufferSize`, exclusive/shared, AAudio/OpenSL are shown in
Settings → Audio → "What we got".

## 2. Rates and the "quality multiplier"

```
firmware 44 100 Hz ──► Resampler ──► Fe (stream rate: 48 000 | 96 000 | native)
                                       │
                                       ▼
                     MasterBus at Fe; non-linear stages ×Q (Q = 1 | 2 | 4 | 8)
```

- **The firmware's internal rate never changes — 44 100 Hz** (SOURCE-ANALYSIS §3, ADR-0003). The UI says so next to
  the setting, so nobody expects "the firmware becomes 96 kHz".
- **Fe 48 000** — the native rate of almost every phone, the lowest latency. **96 000** — less aliasing in the master
  bus and an SRC with a gentler filter; first check whether the device does 96 kHz natively (if not, the system
  resampler kicks in, visible in "What we got").
- **Q (×1…×8)** — oversampling around the master bus's non-linearities (saturation, the final clipper) and the
  true-peak path. A cascade of half-band polyphase IIR filters (HIIR, Laurent de Soras, WTFPL): ×2 = 1 stage,
  ×4 = 2, ×8 = 3; 2 + 2 sections per rate — cheap and NEON-friendly. The oversampler's latency is compensated in the
  dry path (Mix).
- **SRC quality**: "Standard" = 32 taps × 128 phases (passband 0.45·44.1k ≈ 19.8 kHz); "HQ" = 64 taps × 256 phases,
  Kaiser β≈9, passband 0.47. A NEON convolution kernel (4 taps per instruction, L/R together).
- **"Auto"** quality: starting values per device class (fast phone: Fe 48k, Q×4, HQ; low-end phone: Fe 48k, Q×2,
  Standard); a watchdog lowers Q when the callback p99 > 70 % of the budget for 2 seconds in a row; only the user
  raises it.

## 3. Firmware render modes

**Direct (default)** — the firmware renders right inside the Oboe callback. Lowest latency: only the Oboe buffer +
SRC (~0.4 ms).

**Buffered (P5)** — a separate device thread (`THREAD_PRIORITY_URGENT_AUDIO`, the same ADPF session, pinned to the
big cores on MediaTek) renders the firmware N half-buffers ahead (N = 1…4, 2.9 ms each) into an SPSC ring; the
callback only reads + SRC + master. It protects against `ui_draw()` spikes (the firmware's main loop and screen
drawing run inside `render`, once every 15 ms of device time). The price: +N·2.9 ms of input latency.

The mode is chosen in Settings → Audio; when the xrun counter grows, the app suggests Buffered.

## 4. The callback block (Direct)

```
process(out, n):                                   // n = framesPerBurst (usually 96…480)
  for each 64-frame sub-block:
    slot.applyEvents()                             // HOLD_MS gating by fw.nowMs()
    need = resampler.inputNeeded(64)
    fw.render(need, inL, inR)                      // 44.1 kHz
    resampler.process(64, srcL, srcR)              // → Fe
    slot.publish()                                 // LEDs/ui_state every ~16 ms, the screen on request
  master.process(srcL, srcR, n)                    // ×Q inside
  meters.update(); recorder.tap()                  // P8
  denormal guard: FTZ (FPCR.FZ on arm64) at the callback's entry
```

## 5. Budgets and targets

The callback budget = n / Fe (for example 192 / 48 000 = 4.0 ms). Targets on a **heavy song** (`test_heavy`, Felucca
and JIANT), 3 minutes, screen on, LEDs drawn:

| Device | Configuration | Callback p99 | xrun |
|---|---|---|---|
| Samsung Galaxy M52 (778G) | Direct, Fe 48k, Q×4, HQ | ≤ 40 % of budget | 0 |
| POCO C75 (G81 Ultra) | Direct, Fe 48k, Q×2, Standard | ≤ 60 % of budget | 0 |
| POCO C75 | Buffered N=2, Q×4 | ≤ 60 % of the callback budget + the device thread never drains the ring | 0 |

**Direct mode (decided 2026-10-12).** Once every 15 ms of device time the firmware's main loop (UI, screen, sequencer)
runs inside `render()`. On JIANT's heavy song it costs up to ~4 ms and, with a 2 ms burst, falls entirely into one
callback. So on the Samsung p99 ≈ 94 %, max 4.3 ms, with 0 xruns. It is the main loop's peak, not the average load,
and no Direct setting removes it. Hence two rules:

1. **The Direct minimum buffer is 6 ms:** `bufferSize = max(2 × burst, ceil(6 ms × Fe / burst) × burst)`. The Samsung
   gets 288 frames (+2 ms); the POCO does not change.
2. **The Direct gate:** 0 xruns over 3 minutes of the heavy song and the callback max < the buffer duration. The
   "p99 ≤ 40/60 %" targets in the table above apply to Buffered mode and to configurations with the master bus.

Measurement: our own `CpuMeter`. On every callback the audio thread puts the **budget fraction** = duration ÷
(numFrames / Fe) into a histogram of atomics (0–200 % in 0.5 % steps + overflow) and updates the max. A control thread
resets it at the start of a run and computes p50/p99/max **over the whole run**. A ring of "the last N callbacks" is
no good for a gate: it sees only the last half second. A budget from the last callback is no good either: on the POCO's
legacy path callbacks come in 384/576/768 frames. Xruns are read only by a control thread (`getXRunCount()` on the
legacy path takes the AudioTrack mutex); the increase over the run is reported. Perfetto confirms. The thermal state
is recorded.

## 6. MediaTek (POCO C75)

Helio G81 Ultra = 2×Cortex-A75 + 6×Cortex-A55; trying SCHED_FIFO gives EPERM; what works is `sched_setaffinity` of
the callback thread onto the A75 pair (found via `/sys/devices/system/cpu/cpu*/cpu_capacity` or `cpuinfo_max_freq`),
once in the first callback after the stream opens, with a switch in the settings and automatic disabling when xruns
grow. Goes into `engine/Affinity` in P5.

**Latency on the POCO (measured 2026-10-12).** Exclusive does not open; Shared+LowLatency gives a 960 burst and
callbacks of 384–768 frames. `dumpsys media.audio_flinger` shows our track on `AudioOut_D` with Flags `0x000` — it is
**not FAST**: float, FrmCnt 1920, Latency ≈ 98 ms. The output does have a FastMixer (HAL 256 frames), but the only
FAST track in the system there is PCM16. So "tap → sound" on the POCO is ~100 ms + the touch today. To investigate
in P5.

## 7. "Tap → sound" latency (what we control)

The touch (OS ~20–40 ms) + delivery to the UI thread + waiting for the HOLD_MS gate (0 if the previous change was
> 24 ms ago) + waiting for the next firmware half-buffer (0…2.9 ms, as on the hardware) + the Oboe buffer + SRC. An
estimate is shown in Settings → Audio. To minimise it: touches are handled in `pointerInput` without waiting for a
frame, and JNI is called at once.
