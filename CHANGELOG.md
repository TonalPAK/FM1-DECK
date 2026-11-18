# Changelog

## 0.2.1 — four-slot rack and 28 built-in instruments (2026-11-18)

- **Instrument library:** 28 built-in instruments, including SHRUB 1.05. Updated Felucca to 1.8.0,
  JIANT FM to V10.9 and Hortator to 0.26.0.
- **Four-slot rack:** play up to four instruments together and choose them from a picker with thumbnails and
  firmware versions.
- **Rack clock and Ableton Link:** improved shared start and stop timing, including quick restarts. SHRUB follows
  the shared tempo and transport and restores its preset tempo when shared control ends.
- **Live controls:** updated encoder labels and values on supported instruments. The Maschine MK3 bridge shows the
  focused instrument and its live parameter values where available.
- **Mute and Solo:** repeated presses switch each control independently while the rack drawer stays open.
- **Settings:** a larger white ring makes the entry visible at the rounded corner of the screen. The green mark
  inside shows when Ableton Link is on.

## 0.2.0 — three grooveboxes in one (2026-10-13)

- **The rack:** up to three firmwares play at once into the one master bus. A drawer from the triangle in the
  top-right corner switches the panel between them, picks each slot's firmware, mutes / solos, switches a slot off
  with a swipe, and shows each slot's level and CPU share.
- **One clock for the rack:** with two or more firmwares, tempo and PLAY / STOP are shared; a firmware added while
  the others play comes in on the next bar. Tempo in the drawer (drag), or the Link session's when Link is on.
- **Sessions:** Settings → Session saves the whole rack under a name — every firmware with its flash, volumes,
  mute / solo, focus, tempo — and loads it back.
- **DSP meter** under the settings dot and in the drawer.
- **Hortator** (DEADACTIVE's drum machine based on Felucca, DRUM-0.16.0) is the fourth built-in firmware: eight
  drum tracks, steps, Grids, ACID bass, PERFORM.
- **Firmware updates:** Felucca 1.6, JIANT FM 0.6.75.
- **Portrait layout:** a gamepad skin for the phone held upright; Settings lays out for it too.
- **Key light colours** (Settings → Skin) and beat marks every four white keys.
- **Knob names** on the panel now follow the firmware's page (they showed KNOB 1–4 before), also on HOME.
- **Tighter clock:** the MIDI clock the firmwares follow (Link and the rack) is timed from the audio itself, so it no
  longer drifts or doubles a pulse on phones with large or uneven audio bursts.
- Settings' tab list scrolls on short screens.

## 0.1.0 — preview (2026-10-13)

First public preview.

- **Firmwares:** Felucca 1.5.1, JIANT FM 0.6.41, SLOOP 2.6, built from the authors' unmodified sources; switch in
  Settings → Firmware, each with its own flash.
- **Panel:** the original screen, 12 buttons with LEDs, encoders, MASTER, OCT, 27 multitouch keys with note names
  that light up when the firmware offers keys as functions.
- **Six layouts** with schematic thumbnails; keyboard height, knob sensitivity, fine mode, glissando, haptics.
- **Master bus:** dynamic saturation, spectral clipper, look-ahead true-peak limiter, final clipper; 7 presets and
  your own, A/B, bypass, auto-gain, live scope; quality ×1–×8.
- **Ableton Link:** shared tempo, beat and start/stop between phones and apps on the same Wi-Fi.
- **F (latch)** mini button: latch layer buttons for one-handed play.
- **App icon**: a micro panel behind a colour FM wave.
- The app and its documentation are in English; layout names in Settings → Skin are English.
- Settings dot stays visible inside rounded screen corners; MASTER drags smoothly (it no longer stalls after a step).
