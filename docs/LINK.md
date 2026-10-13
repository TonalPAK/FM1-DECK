# Ableton Link — sync over Wi-Fi

Phones on the same network play together: a shared **tempo**, a shared **phase** (downbeats line up) and a shared
**start/stop**. The protocol is Ableton Link (`third_party/link`, GPL-2.0-or-later); Ableton Live and hundreds of
apps sync with it too.

## How to use it

1. Both phones on the same Wi-Fi network.
2. Settings → MIDI → **LINK ON** on each. "Linked with N devices" means the session is up; the settings dot in the
   panel's corner is green while Link is on.
3. **PLAY** on either phone's panel starts all of them; the start lands on the nearest "Start on" boundary (beat /
   2 beats / bar / 2 bars, bar by default). PLAY again stops all of them. GLO+PLAY, REC+PLAY and other combos work as
   on the device (they are not the shared transport).
4. Tempo: −1 / +1, TAP, or drag the number. It changes everywhere.

**No roles.** Link has no leader and no follower: every participant is equal, anyone can change the tempo, the others
follow. A newcomer takes the tempo of the session already running.

Link is off at every app start: the network is used only on a user action. Only "Start on" is remembered.

## How it works

- `engine/LinkSync` holds `ableton::Link` (created when first switched on). Once per callback the audio thread takes
  `captureAudioSessionState()` and computes when the buffer's first frame will **be heard**: `clock.micros()` + output
  latency (Oboe `calculateLatencyMillis`, refreshed from a control thread) + master-bus latency.
- Before each firmware sub-block (64 frames, the `FirmwareSlot::process` hook) `ClockPulser` turns the Link beat into
  MIDI clock: 24 F8 pulses per beat, FA on beat 0 of a start, FC on stop. Accuracy is half a sub-block (0.67 ms at
  48 kHz). The firmware in external-clock mode (`host_clock(1)`: Felucca/JIANT CLK, SLOOP SYNC) follows the pulses.
- Start: on press, `setIsPlaying(true)`; every participant, on its own audio thread, calls
  `requestBeatAtStartPlayingTime(0, quantum)` — beat 0 lands on the nearest quantum boundary in phase with the session.
- A plain PLAY is intercepted in `nativePanel` (`LinkSync::filterButtons`) and does not reach the firmware while Link
  is on.
- Android filters multicast on Wi-Fi: while Link is on, a `WifiManager.MulticastLock` is held. Permissions:
  `INTERNET`, `CHANGE_WIFI_MULTICAST_STATE`.
- Switching Link off gives the firmware its internal clock back (only if we switched it: a CLK set by hand is left
  alone).

## Verified (2026-10-12)

A Samsung Galaxy M52 + a POCO C75 on one network: "Linked with 1 device" on both; +2 BPM on the Samsung → 122 on the
POCO; PLAY on the Samsung started both, PLAY on the POCO stopped both. Output latency: Samsung ≈ 13 ms, POCO ≈ 92 ms
(shared, buffer 1920) — compensated. Test `native-tests/test_clock_pulser.cpp`: 24 PPQN, accuracy, FA on beat 0, FC,
no bursts on jumps.

## Next

- Notes between phones and with a computer: network MIDI (RTP-MIDI) — P9.
- Send a tempo changed with the firmware's knob into Link (today the tempo is set in the app).
