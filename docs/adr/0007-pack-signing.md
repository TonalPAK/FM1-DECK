# ADR-0007: Firmware packages are signed with Ed25519; unsigned ones only behind an explicit switch

Status: accepted.

A native package is executable code with the app's permissions. An Ed25519 signature over `manifest.json` (which
holds the SHA-256 of every file), verified in native code (Monocypher, CC0/BSD-2), with the public keys built into
the app. Unsigned packages only with the "Developer" switch and a warning; WASM-only packages are sandboxed, so the
warning is softer. A new package's trial load runs in a separate process, `:fwprobe`.
