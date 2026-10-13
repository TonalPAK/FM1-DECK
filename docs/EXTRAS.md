# Extra features (after the MVP) — planned

## 1. Firmware over Wi-Fi from a PC (P8) — like a web installer

- Settings → Firmware → "Wi-Fi from PC": the app starts an HTTP server **on the Wi-Fi interface only**, on a random
  port, while the dialog is open (auto-stop after 10 min). The screen shows the address `http://192.168.x.y:PORT`,
  a QR code and a one-time 6-digit PIN.
- The page (built-in HTML, no external resources): a PIN field, a drag & drop area, progress; it accepts `.mwfw`
  (and `.wasm`, overlays, flash backups) up to 64 MB, `POST` multipart; the PIN goes in a header, never in the URL.
- The file → the normal install pipeline (FIRMWARE-PACKAGES §4) → **confirmation on the phone** is mandatory.
- Implementation: a small server of our own on `ServerSocket` in its own thread (or NanoHTTPD, BSD-3); nothing sent
  is executed without verification. Permissions: `INTERNET`, `ACCESS_WIFI_STATE`.

## 2. USB/BLE MIDI input and clock (P9)

- `android.media.midi.MidiManager`: USB devices and BLE MIDI (scanning via `BluetoothLeScanner`, permissions
  `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT` on request).
- A MIDI receiver on its own thread → a lock-free queue into `FirmwareSlot` → `fw.midi()` at the start of a block.
- Channel messages go to every backend; clock F8/FA/FB/FC only with `FM1_CAP_HOST_CLOCK` (native): `host_clock(1)`
  is switched on and the firmware keeps CLK = USB. Without the capability the "external clock" item is disabled,
  with an explanation.
- MIDI out (clock/notes from the firmware) is out of scope (the emulator has no USB stack); to revisit after P9.

## 3. WAV recording (P8)

- A tap after the master bus (or before it) into a lock-free ring (≈ 2 s at Fe) → a writer thread →
  `recordings/*.wav` (24-bit PCM or float32, rate Fe); the header is completed on stop; data lost to an overflow is
  counted and shown.
- The record button lives in the menu and (optionally) on a long tap on the firmware screen; a REC indicator on top
  of the panel.
- Export: "Share" (FileProvider) and "Save to…" (SAF).

## 4. Exchange with a real FM-1 (P9)

Step 1 — **compatibility with the browser emulator**: flash export/import in the sector format
(`worklet.js storeSectors/restoreSectors`, the `STORE_AREAS` regions from the manifest) — projects move between the
phone and the author's web emulator.

Step 2 — **research (spike)**: the FM-1 connects to the phone over USB-OTG as a USB-MIDI device; the firmwares' web
editor speaks a SysEx protocol (`web/EDITOR_PROTOCOL.md` in Felucca/JIANT/SLOOP, backup — `web/fm1backup.js`).
Goal: "Download projects from the FM-1" / "Write projects to the FM-1" from the app via `MidiManager` + SysEx, with
the same protocol. The spike's result is a report: which commands are needed, whether the project format matches the
emulator's flash, the risks (firmware versions on the hardware and in the app must match). Implementation only after
the report is reviewed. Flashing the FM-1 itself from the app (`.fwsc`, `fm1ota.js`) is **out of scope** (the risk
of bricking the device).
