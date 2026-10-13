# ADR-0003: The firmware always runs at 44.1 kHz; "48–96 kHz" and "×2…×8" are SRC and the master bus

Status: accepted.

**Context.** Higher output rates (48–96 kHz) and a quality multiplier ×2…×8 were wanted. The firmwares are written
in fixed point with constants computed for FS = 44 100 (`eng_formant.c:73-74`, `eng_noise.c:148`, `eng_lofi.c:362`,
`click.c`).

**Decision.** The firmware's internal rate never changes. Firmware output → polyphase SRC (32/64 taps) → engine rate
Fe (48 000 / 96 000 / native) → master bus; the master bus's non-linear stages and true-peak detection run
oversampled ×Q (Q = 1/2/4/8, HIIR half-band IIR), the saturator and clippers also with ADAA.

**Consequences.** The firmwares sound as their authors made them; weight and cleanliness come from the master bus,
without aliasing. The UI labels the setting honestly.
