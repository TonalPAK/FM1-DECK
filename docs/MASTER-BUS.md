# Master bus: dynamic saturation · spectral clipper · limiter · clipper

Our own implementation (C++17, NEON-friendly, no allocations in the callback). The feel it aims for is the class of
spectral clippers like Newfangled Audio Saturate (clipping that treats frequencies separately so a big bass does not
"choke" the detail; a curve shape from a hard clip to the smoothest possible; symmetry for even harmonics; a ceiling;
anti-aliasing). The code and algorithms are our own, from open publications; Saturate is only a sound reference.
Parameters: [`MasterBusParams.h`](../app/src/main/cpp/dsp/MasterBusParams.h).

```
in ─► [0 Input] ─► [1 Dynamic saturation ×Q] ─► [2 Spectral clipper + peak return]
   ─► [3 Look-ahead limiter, true peak] ◄─► [4 Final clipper ×Q] ─► [Output, Mix, Meters] ─► out
```

## 0. Input

- Protection: NaN/Inf → 0 (and a counter in debug), a clip at ±64 before anything else.
- DC blocker: a one-pole 5 Hz high-pass.
- `INPUT_GAIN` −24…+24 dB (20 ms smoothing).
- Input meters: peak L/R, RMS (300 ms).

## 1. Dynamic saturation (`SAT_*`) — "saturation that follows the dynamics"

1. **Envelope**: an RMS detector on mid `(L+R)/2`, attack = `SAT_DYN_SPEED`/4, release = `SAT_DYN_SPEED`.
   `e_dB` relative to a −18 dBFS reference, limited to [−24, +12].
2. **Drive**: `drive_dB = SAT_DRIVE + SAT_DYNAMICS · 0.5 · e_dB`.
   `DYNAMICS > 0` — loud parts saturate more (density and "glue" on peaks), `< 0` — quiet parts are lifted with
   harmonics (saturation as an "upward" compression), `0` — static drive. The drive is smoothed (5 ms) so there are no
   modulation clicks.
3. **Tonal tilt**: `SAT_TILT` — a ±6 dB shelving tilt around 800 Hz *before* the curve and the exact inverse
   correction *after*: the tonal balance is kept, while harmonics are born mostly in the highs (TILT>0) or lows (<0).
4. **Curve** (a family with closed-form antiderivatives — needed for ADAA):
   - `SAT_SHAPE` 0…0.5: a blend `hardclip(x)` → `tanh(x)`; 0.5…1: `tanh(x)` → `x/√(1+x²)` (the smoothest);
   - antiderivatives: hardclip — piecewise `x²/2` / `|x|−1/2`; tanh — `log cosh x`; algebraic — `√(1+x²)`;
     a blend of antiderivatives is linear → **first-order ADAA** (Parker, Zavalishin, Le Bihan, DAFx 2016) works for
     any shape; at |Δx| < 1e-5 — the midpoint formula.
5. **Symmetry** `SAT_SYMMETRY`: an offset `b = 0.3·sym` before the curve, `y = f(g·x + b) − f(b)`, then a DC blocker
   → even harmonics.
6. **Level compensation**: `y /= f(g·r)/r` at r = 0.25 (≈ −12 dBFS) — loudness at medium levels barely changes as the
   drive grows (a fair A/B).
7. The whole stage (items 3–6) runs inside the ×Q oversampler; ADAA further cleans aliasing.
8. `SAT_MIX` — a parallel blend with a delay-aligned dry signal.

Meter: `SAT_AMOUNT` — the average degree of non-linearity (1 − y/x by RMS) 0…1.

## 2. Spectral clipper (`SPC_*`)

1. **Crossover**: 4th-order Linkwitz-Riley, a tree with allpass compensation (the sum of the bands = an allpass: flat
   magnitude, the same phase for every band). Split frequencies (`SPC_BANDS`):
   - 4 bands: 150 · 1 000 · 5 000 Hz
   - 6 bands: 120 · 400 · 1 200 · 3 500 · 9 000 Hz
   - 8 bands: 90 · 220 · 500 · 1 100 · 2 400 · 5 000 · 10 000 Hz
2. **Band clip**: `y_b = c_b · f_shape(x_b / c_b)` (the same curve as in §1, `SPC_SHAPE`, ADAA1), where
   `c_b = C · w_b`, `C = 10^(SPC_CEILING/20)`, `w_b` — weights from equal-loudness curves (the lows are clipped
   earlier so the bass does not take the headroom of the mids and highs).
3. **Spread** (`SPC_SPREAD`) — from a plain clipper to a spectral one:
   - a broadband factor `g_bb[n] = clip(Σx_b)/Σx_b` (a plain clipper of the whole sum);
   - `out = Σ_b lerp(g_bb·x_b, y_b, spread)`. 0 = a classic clipper, 1 = fully independent bands.
4. **Peak return** (`SPC_PEAK_MIX`, "how much of the peaks to mix back"): a transient detector on the sum:
   `env_fast` (1/20 ms), `env_slow` (20/200 ms), `t = clamp((env_fast − env_slow)/env_slow · k(SPC_PEAK_SENS), 0, 1)`;
   `out += PEAK_MIX · t · (x − out)` — attacks cut by the clipper partly come back (punch), the steady part stays
   dense. The limiter/final clipper catch them afterwards.
5. Meter `SPC_GR` — the average attenuation of the sum (dB).

The stage runs at Fe (no ×Q): the crossover would be 8× as expensive; aliasing is tamed by ADAA and by band clipping
being softer.

## 3. Look-ahead limiter (`LIM_*`)

1. The signal is delayed by `L = LIM_LOOKAHEAD` ms (0.5…5).
2. Detector: sample peak or **true peak** (`LIM_TRUE_PEAK`): 4× polyphase FIR interpolation (in the spirit of
   ITU-R BS.1770-4 Annex 2) → the maximum of inter-sample peaks.
3. The required gain `g_req = min(1, ceiling/peak)`, stereo link `LIM_LINK` (0 = separate, 1 = min over channels).
4. **No overshoot**: a sliding minimum of `g_req` over a window L (a monotonic deque, O(1)) → attack smoothing with a
   moving average of length L (boxcar) → the gain reaches the target exactly when the peak leaves the delay line.
   Release is one-pole `LIM_RELEASE`; **auto release**: two time constants (a fast 30 ms for single peaks, a slow one
   = `LIM_RELEASE` for sustained GR), blended by how long the attenuation lasts.
5. Guarantee: |y| ≤ ceiling per sample; in TP mode ≤ ceiling dBTP within 0.1 dB; the final clipper (if on) is the
   safety net.
6. Meter `LIM_GR`.

## 4. Final clipper (`CLIP_*`)

A hard/soft clip at the limiter's ceiling (`CLIP_SHAPE`: 0 = hard, 1 = soft with a 3 dB knee), ADAA1, inside ×Q.
`CLIP_POSITION`: 0 — after the limiter (a safety net), 1 — before the limiter ("loud" mode: the clipper shaves sharp
peaks, the limiter works less and "breathes" less often). Meter `CLIP_GR`.

## 5. Output and service

- `OUTPUT_GAIN` −24…+12 dB; `MIX` — dry/wet of the whole bus with compensation of the total latency (oversampler +
  look-ahead).
- `AUTO_GAIN` — for listening comparisons only: matches the output loudness to the input by LUFS-S (3 s), smoothly.
- `BYPASS` — a 20 ms crossfade to the delayed dry signal (no click, no latency jump).
- Meters (~30 Hz in the UI): input and output peaks/RMS, GR of every stage, output true peak, **short-term LUFS**
  (BS.1770 K-weighting, 3 s window), DSP load.
- The bus's total latency is reported to the engine and shown in Settings → Audio.

## 6. Presets (built-in, `assets/presets/master/*.json`)

| Preset | Idea |
|---|---|
| Default | the everyday chain: light saturation, gentle spectral clipping, a true-peak limiter |
| Clean | everything off except the limiter at −0.3 dBTP (overload protection) |
| Warm | SAT drive 6, shape 0.7, sym 0.15, dyn +0.3; SPC off; limiter −0.5 |
| Punch | SAT 4; SPC ceiling −2, spread 0.8, **peak mix 0.45**; limiter −0.3 TP, release 80 |
| Loud | SAT 8; SPC ceiling −3, spread 0.7, peak mix 0.2; CLIP before the limiter; limiter −0.2 TP |
| Crush | SAT 24, shape 0.1, sym 0.4, dyn +0.8, tilt +0.5; SPC 4 bands, spread 0.3 |
| Glue | SAT 3, dyn +0.6, speed 200; SPC ceiling −1.5, spread 0.5; limiter release auto |

User presets are saved in `presets/master/`; the UI compares two snapshots A/B.

## 7. Performance (initial estimates)

| Stage | Fe 48k, Q×1 | Q×4 | Q×8 |
|---|---|---|---|
| Saturation (stereo, ADAA, oversampler) | ~1.5 % of an A55 core | ~5 % | ~10 % |
| Spectral clipper, 8 bands | ~4 % | — | — |
| TP limiter | ~2 % | — | — |
| Final clipper | ~0.5 % | ~2 % | ~4 % |

These were estimates; the measured numbers are in §8a.

## 8. Tests

A null test with every stage off; the ceiling is never exceeded (peak and TP) on noise, sines, a 0 dBFS square,
impulses, a 20 Hz–20 kHz sweep; no NaN/Inf on silence and DC; a jump of every parameter without clicks (derivative
analysis); aliasing: a 9 kHz + 11 kHz sine at 24 dB drive — the alias level falls at Q×1/×2/×4/×8; NEON == scalar
within 1e-6 relative.

## 8a. Implementation decisions (2026-10-12)

- **The oversampler uses our own linear-phase half-band FIRs** (`dsp/Oversampler.h`) instead of HIIR polyphase IIRs:
  with linear phase the dry path of the parallel blends (`SAT_MIX`, `MIX`, `BYPASS`) is aligned with an integer delay,
  no comb filtering. A 63 / 19 / 11-tap cascade (Kaiser), latency ×2 = 31, ×4 = 36, ×8 = 37 samples. As with any
  half-band filter the transition band is symmetric around 24 kHz: content at 24–28 kHz folds into 20–24 kHz (above
  hearing) — accepted; aliases in the audible band are checked by a test.
- **ADAA is applied to the curve's residual** `r(x) = f(x) − x`, not to the whole curve: classic ADAA1 in the linear
  region is the average of two neighbouring samples (−2 dB at 10 kHz, −11 dB at 20 kHz at 48 kHz). The residual form
  is transparent at normal levels (10 kHz at −40 dBFS: 0.006 dB) and keeps the alias suppression.
- **ADAA at every Q**, not only ×1/×2: without it a hard clip at ×4 gives more aliasing (−38 dB) than ×2 with it
  (−43 dB). Measured (hard clip, 9 kHz, 24 dB drive, aliases < 20 kHz): ×1 −37.5, ×2 −43.1, ×4 −75.9, ×8 −76.6 dB.
- **The limiter's true-peak detector has 193 taps** (4× polyphase, latency 24 samples = 0.5 ms) with parabolic
  refinement of the maximum: BS.1770's 48 taps under-read peaks of content near Nyquist (the spectral clipper's
  output). If a sample's neighbourhood is more than 6 dB below the ceiling, interpolation is skipped. The output meter
  uses 49 taps.
- **Changing the spectral clipper's band count**: the stage goes to bypass for 10 ms (the dry signal; it has no
  latency), switches and comes back — instead of a parallel crossfade of two configurations.
- **Changing the latency** (Q, `LIM_LOOKAHEAD`, `CLIP_POSITION`): the whole bus fades to zero in 10 ms, rebuilds and
  comes back. The UI sends stepped parameters once — at the end of the gesture.
- The spectral clipper's band weights follow the ISO 226 equal-loudness curves (0.55 at 60 Hz → 1.0 at 1 kHz, flat up
  to 5 kHz, 0.9 at 16 kHz), constants in `SpectralClipper.h`.

### Performance (measured, 48 kHz, "Default", the bus's share of the callback budget)

| Phone | ×1 | ×2 | ×4 | ×8 |
|---|---|---|---|---|
| Samsung M52 (burst 96, buffer 288) | 17 % | 20–25 % | 34 % | 41 % |
| POCO C75 (burst 960, buffer 1920) | 11 % | 17–25 % | 31 % | 41 % |

0 xruns in every mode. That is more than the §7 estimates: P5 targets NEON for the FIRs/biquads, pinning to the big
cores on MediaTek and auto quality. The default is ×2.

## 9. Sources

Parker, Zavalishin, Le Bihan, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution"
(DAFx 2016) — ADAA; ITU-R BS.1770-4 — true peak and LUFS; ISO 226 — equal-loudness curves; Linkwitz-Riley crossovers —
a well-known design; HIIR (Laurent de Soras, WTFPL) — considered for oversampling. Airwindows (MIT) — only as a
listening reference for curves. Third-party code, if any, is listed in `THIRD_PARTY_LICENSES.md`.
