# `.mwfw` firmware packages and "drop it in — it updates" (planned, P3)

The goal: a new version of JIANT/Felucca/SLOOP appears at its author → minutes later it is a ready package → the user
installs it on the phone in one action (a file, "Open with", a "Check for updates" button, or drag & drop in a PC
browser over Wi-Fi), with rollback and a flash backup.

Today the three firmwares are built into the APK; the package pipeline below is the plan.

## 1. Package format

`.mwfw` = ZIP (deflate, no encryption), named `<id>-<version>-<sha8>.mwfw`:

```
manifest.json                    the description (below), holds the sha256 of every other file
manifest.sig                     Ed25519 signature over the bytes of manifest.json (64 bytes), may be absent
native/arm64-v8a/libfw.so        the Fm1FirmwareApi module (ABI 1)
native/x86_64/libfw.so           for the Android emulator (optional)
wasm/firmware.wasm               the same firmware as WebAssembly (optional, recommended)
wasm/firmware-arm64-v8a.aot      WAMR AOT (optional, P7)
overlay/overlay.json             key hints (docs/KEY-OVERLAY.md) + overlay.<lang>.json translations
docs/…                           cheat sheets (png/pdf), MANUAL.md — opened from the app (optional)
LICENSE, LICENSING.md, LICENSES/ the upstream licences (mandatory, GPL)
SOURCE.md                        links to the exact upstream and fwbuild commits + the build command (mandatory, GPL)
```

Import limits: size ≤ 64 MB, ≤ 256 files, unpacked ≤ 128 MB, paths only under allowed prefixes, no `..`, absolute
paths or symlinks (zip-slip protection).

## 2. `manifest.json` (schema 1)

```json
{
  "schema": 1,
  "id": "jiant",
  "name": "JIANT FM",
  "version": "v0.6.41",
  "channel": "nightly",
  "upstream": { "repo": "https://github.com/juanjiant-bit/JIANT", "commit": "337232749c8ab304726b194e9a86b36b4a5932ae", "date": "2026-10-12" },
  "build": { "fwbuild": "<git sha of FM1 DECK>", "ndk": "28.2.13676358", "emsdk": "6.0.11", "date": "2026-10-12T14:03:00Z" },
  "abi": 1,
  "sampleRate": 44100,
  "screen": { "w": 240, "h": 240, "format": "rgb565be" },
  "nor": { "size": 1048576, "storeAreas": [[618496, 917504], [937984, 946176], [1032192, 1048576]] },
  "buttons": ["FX","SCL","ENV","LFO","EDIT","GLO","HOME","SAVE","ARP","SEQ","PLAY","REC","OCT-","OCT+"],
  "capabilities": { "native": ["host_clock","save_now","ui_state","seed","anim","mid_keys","test_heavy"], "wasm": ["seed","anim","mid_keys","test_heavy"] },
  "flash": { "migration": "backup-first", "note": "0.6.x keeps the 0.5 project format" },
  "minApp": "0.3.0",
  "files": { "native/arm64-v8a/libfw.so": "sha256:…", "wasm/firmware.wasm": "sha256:…", "overlay/overlay.json": "sha256:…" },
  "license": "GPL-3.0-only",
  "signer": "fm1deck-release-2026"
}
```

`flash.migration`: `compatible` (the flash fits as is) · `backup-first` (default: back up and try) ·
`reset-required` (the new version cannot read the old flash: back up, then erase; the user is warned).
`storeAreas` are the flash regions the browser emulator stores (`worklet.js STORE_AREAS`), for exchanging saves with
the browser (EXTRAS.md §4).

## 3. Signing and trust

- A project Ed25519 key (`signer` → the public key is built into the app; several are possible, revocable by version).
- Verification: Ed25519 over the exact bytes of `manifest.json`, then the SHA-256 of every file in `files` (extra
  files are an error). Implementation: native, vendored **Monocypher** (CC0/BSD-2, `monocypher-ed25519` for SHA-512
  Ed25519).
- Signed by CI (secret `FM1DECK_SIGNING_KEY`) or locally (`tools/fwpack/fwpack.py sign`).
- **An unsigned/foreign package**: only if Settings → Firmware → "Developer: allow unsigned" is on, with a red warning
  "native code will get the app's permissions". A package with only a `.wasm` runs in a sandbox — the warning is
  softer for it.

## 4. Installing

1. Import → a copy into a temporary file `cacheDir/import/<uuid>.mwfw` (the SAF stream is read once).
2. The checks of §1 and §3; reading `manifest.json`; compatibility: `abi`, `minApp`, the device's ABI or wasm present.
3. A confirmation screen: name, version was → will be, source (commit), signature (✓ / ⚠), capabilities, what happens
   to the flash (`flash.migration`), size.
4. Unpacking into `fw/<id>/<version>-<sha8>.tmp/` → `File.setReadOnly()` on every file → an atomic rename.
5. **A trial run in the `:fwprobe` process**: dlopen (or WAMR), ABI check, boot, 1 s of rendering silence, checks that
   samples are finite and render time is sane → OK/error back via Messenger. A crashing probe does not take the app
   down.
6. A flash backup `flash/<id>/backups/<ts>-<oldver>.nor`; writing `fw/state.json` (the new version active, the old one
   in history). The last 3 versions of each firmware are kept.
7. If the firmware is active — park → unload → load the new one with the current flash → unpark (with an output
   crossfade).

**Rollback** (Firmware → History): restores the previous version **and** the flash backup made before the update.

> Android 10+: executing files from the app's home directory via `exec()` is forbidden; `dlopen` of libraries from
> `filesDir` is allowed, and Android 14 requires dynamically loaded files to be read-only. This is a P3 gate on real
> phones. If `dlopen` from `filesDir` fails on some device, the same package goes through the WASM backend, and the
> built-in versions (in `nativeLibraryDir`) always work.

## 5. Where packages come from

| Source | How | Phase |
|---|---|---|
| Built-in | built into the APK from `fwbuild/` (one version of each firmware) | done |
| File | Settings → Firmware → "Install from file" (SAF, `*/*` + a `.mwfw` filter) | P3 |
| "Open with" / "Share" | intent-filter `ACTION_VIEW`/`ACTION_SEND` for `.mwfw` and `application/zip` | P3 |
| Update check | on tap: `GET https://api.github.com/repos/<owner>/<repo>/releases` → `*.mwfw` assets + `firmware-index.json` | P3 |
| Wi-Fi from PC | the app serves a page on the local network; drag the file in the PC's browser | P8 |
| Upstream `.wasm` directly | "Import .wasm" (for example `felucca.wasm` from the author's GitHub Pages) → the app wraps it into a package itself (unsigned, WASM only) | P7 |

`firmware-index.json` (in every release): `[{id, version, channel, url, sha256, size, date, upstreamCommit}]`.
Channels: `release` (versions the author marked as a release/tag) and `nightly` (every new upstream commit).

## 6. CI: `.github/workflows/firmware-packs.yml` (P3)

```
on: schedule (every 6 h) + workflow_dispatch(id, ref)
jobs:
  detect:  for each id compare the upstream HEAD with the last built commit (firmware-index.json) → a matrix
  build (ubuntu-24.04, for each new commit):
    checkout FM1 DECK + upstream/<id>@commit
    pip install Pillow numpy fonttools  (+ libraqm0 from apt)
    tools/fwgen/fwgen.py <id>                         → build/fwgen/<id>/gen
    cmake fwbuild (NDK r28, arm64-v8a and x86_64)     → libfw.so  (on failure: retry with the capability off)
    emsdk 6.0.11: tools/wasm/build_wasm.sh <id>       → firmware.wasm (+ wamrc → .aot, P7)
    golden test: tools/render (native x86_64 host build) vs Node+wasm  → must match
    overlay: extract (JIANT) / watch README/GUIDE changes (Felucca/SLOOP → an "update the hints" issue)
    tools/fwpack/fwpack.py build + sign               → out/packs/*.mwfw
  release: GitHub Release "fw-<id>-<version>-<sha8>" + an updated firmware-index.json (nightly/release channel)
```

The same locally: `python tools/fwpack/fwpack.py all --ids jiant` (on Windows, generate `gen/` through WSL).

## 7. Why not Google Play

Play policy forbids downloading executable code outside Play — that kills "drop in a firmware". Distribution:
GitHub Releases + Obtainium (ADR-0002). The APK is signed with the project key; Obtainium updates the APK by itself.
