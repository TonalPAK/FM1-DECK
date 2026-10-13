#pragma once

// FM1 DECK master bus: the parameter contract (single source of truth).
//
// Stable numeric IDs (never renumber, never reuse: saved presets and settings store them). Kotlin reads this table
// at runtime through JNI (nativeMasterParamTable), so there is no hand-copied mirror to drift.
// DSP design and ranges explained in docs/MASTER-BUS.md.
//
// FM1_MASTER_PARAMS(X): X(id, KEY, group, min, max, default, unit, kind)
//   kind: C = continuous (smoothed in the DSP, 20 ms one-pole unless noted in MASTER-BUS.md)
//         S = stepped / switch (applied at a block boundary with a short crossfade where the signal path changes)
//   unit: "dB", "ms", "%", "x" (raw 0..1 shown as %), "" (switch / enum index)

#define FM1_MASTER_PARAMS(X)                                                              \
    /* global */                                                                          \
    X(0,  BYPASS,          GLOBAL,   0.0f,   1.0f,   0.0f,  "",   S)                      \
    X(1,  INPUT_GAIN,      GLOBAL, -24.0f,  24.0f,   0.0f,  "dB", C)                      \
    X(2,  OUTPUT_GAIN,     GLOBAL, -24.0f,  12.0f,   0.0f,  "dB", C)                      \
    X(3,  MIX,             GLOBAL,   0.0f,   1.0f,   1.0f,  "x",  C)                      \
    X(4,  AUTO_GAIN,       GLOBAL,   0.0f,   1.0f,   0.0f,  "",   S)                      \
    /* stage 1: dynamic saturation */                                                     \
    X(10, SAT_ENABLE,      SAT,      0.0f,   1.0f,   1.0f,  "",   S)                      \
    X(11, SAT_DRIVE,       SAT,      0.0f,  36.0f,   6.0f,  "dB", C)                      \
    X(12, SAT_SHAPE,       SAT,      0.0f,   1.0f,   0.6f,  "x",  C)                      \
    X(13, SAT_SYMMETRY,    SAT,     -1.0f,   1.0f,   0.0f,  "x",  C)                      \
    X(14, SAT_DYNAMICS,    SAT,     -1.0f,   1.0f,   0.5f,  "x",  C)                      \
    X(15, SAT_DYN_SPEED,   SAT,      5.0f, 500.0f,  80.0f,  "ms", C)                      \
    X(16, SAT_TILT,        SAT,     -1.0f,   1.0f,   0.0f,  "x",  C)                      \
    X(17, SAT_MIX,         SAT,      0.0f,   1.0f,   1.0f,  "x",  C)                      \
    /* stage 2: spectral clipper */                                                       \
    X(20, SPC_ENABLE,      SPECTRAL, 0.0f,   1.0f,   1.0f,  "",   S)                      \
    X(21, SPC_CEILING,     SPECTRAL,-12.0f,  0.0f,  -1.0f,  "dB", C)                      \
    X(22, SPC_SHAPE,       SPECTRAL, 0.0f,   1.0f,   0.5f,  "x",  C)                      \
    X(23, SPC_SPREAD,      SPECTRAL, 0.0f,   1.0f,   0.7f,  "x",  C)                      \
    X(24, SPC_BANDS,       SPECTRAL, 0.0f,   2.0f,   0.0f,  "",   S) /* 0: 4, 1: 6, 2: 8 bands */ \
    X(25, SPC_PEAK_MIX,    SPECTRAL, 0.0f,   1.0f,   0.0f,  "x",  C) /* transient (peak) return */ \
    X(26, SPC_PEAK_SENS,   SPECTRAL, 0.0f,   1.0f,   0.5f,  "x",  C)                      \
    /* stage 3: look-ahead limiter */                                                     \
    X(30, LIM_ENABLE,      LIMITER,  0.0f,   1.0f,   1.0f,  "",   S)                      \
    X(31, LIM_CEILING,     LIMITER, -6.0f,   0.0f,  -0.3f,  "dB", C)                      \
    X(32, LIM_LOOKAHEAD,   LIMITER,  0.5f,   5.0f,   1.5f,  "ms", S) /* changes latency: re-primed */ \
    X(33, LIM_RELEASE,     LIMITER, 10.0f, 1000.0f,120.0f,  "ms", C)                      \
    X(34, LIM_AUTO_RELEASE,LIMITER,  0.0f,   1.0f,   1.0f,  "",   S)                      \
    X(35, LIM_TRUE_PEAK,   LIMITER,  0.0f,   1.0f,   1.0f,  "",   S)                      \
    X(36, LIM_LINK,        LIMITER,  0.0f,   1.0f,   1.0f,  "x",  C)                      \
    /* stage 4: final clipper */                                                          \
    X(40, CLIP_ENABLE,     CLIPPER,  0.0f,   1.0f,   1.0f,  "",   S)                      \
    X(41, CLIP_SHAPE,      CLIPPER,  0.0f,   1.0f,   0.2f,  "x",  C)                      \
    X(42, CLIP_POSITION,   CLIPPER,  0.0f,   1.0f,   0.0f,  "",   S) /* 0: after limiter, 1: before */

enum class MasterParamId : int {
#define FM1_X_ENUM(id, key, group, mn, mx, def, unit, kind) key = id,
    FM1_MASTER_PARAMS(FM1_X_ENUM)
#undef FM1_X_ENUM
};

// Read-only meters published by the audio thread (atomics, ~30 Hz to the UI). Index = order below.
#define FM1_MASTER_METERS(X)            \
    X(0,  IN_PEAK_L,   "dBFS")          \
    X(1,  IN_PEAK_R,   "dBFS")          \
    X(2,  IN_RMS,      "dBFS")          \
    X(3,  SAT_AMOUNT,  "x")             \
    X(4,  SPC_GR,      "dB")            \
    X(5,  LIM_GR,      "dB")            \
    X(6,  CLIP_GR,     "dB")            \
    X(7,  OUT_PEAK_L,  "dBFS")          \
    X(8,  OUT_PEAK_R,  "dBFS")          \
    X(9,  OUT_TRUE_PEAK, "dBTP")        \
    X(10, OUT_LUFS_S,  "LUFS")          \
    X(11, DSP_LOAD,    "%")

enum class MasterMeterId : int {
#define FM1_X_METER(id, key, unit) key = id,
    FM1_MASTER_METERS(FM1_X_METER)
#undef FM1_X_METER
    COUNT
};
