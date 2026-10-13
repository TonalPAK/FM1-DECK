# ADR-0001: Firmwares run as native .so (main path) + WASM (fallback)

Status: accepted (2026-10-12).

**Context.** The real FM-1's `.fwsc` is code for the JieLi AC79 chip and cannot run on a phone. Felucca and JIANT
ship a browser emulator: its HAL `felucca_web.c` includes the unmodified firmware sources, and Emscripten turns it
into a standalone `.wasm` with no imports and about 30 `web_*` exports. The same HAL also builds into a `.so` with
the NDK.

**Decision.** The main backend is `libfw_<id>.so` implementing the `Fm1FirmwareApi` ABI (our glue on top of
`web_*`), built by our CI from the upstream sources. The fallback is WAMR running a `.wasm` (the author's or ours),
with AOT for speed. Both sit behind one C++ interface, `IFirmware`.

**Consequences.** + top speed and every capability (`save_now`, `host_clock`, `ui_state`);
+ WASM gives a sandbox, portability and "take the file straight from the author's site" on release day;
− two backends in code and tests (a native == wasm golden test pays for it); − native packages need signing.
