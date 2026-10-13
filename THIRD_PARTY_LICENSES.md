# Third-party components and licences

FM1 DECK is licensed **GPL-3.0-only** (it executes and distributes GPL-3.0-only firmwares). Every component below is
compatible with GPL-3.0. Keep this file current: a phase that adds code from elsewhere adds a row here.

| Component | Where | Licence | Notes |
|---|---|---|---|
| Felucca (Leo Kuroshita / Hügelton Instruments) | `upstream/felucca` (submodule), firmware packs | GPL-3.0-only | unmodified; includes OFL fonts, msfa (Apache-2.0), Fukiai font (MIT) — see its `LICENSING.md` |
| JIANT FM (juanjiant-bit) | `upstream/jiant`, packs | GPL-3.0-only | fork of Felucca; song mode from SLOOP |
| SLOOP (isod89) | `upstream/sloop`, packs | GPL-3.0-only | based on Felucca |
| Browser-emulator HAL design (X0X by charlesvestal, Felucca `web/emu`) | used via upstream `felucca_web.c` | GPL-3.0 | not copied, built from upstream |
| SLOOP host HAL `fwbuild/hal/sloop/sloop_web.c` | fwbuild | GPL-3.0-only | first written for GGBOX (an earlier port by the same author), adapting Felucca's `felucca_web.c` |
| GGBOX MW port (the same author) | ported into `fw/`, `dsp/Resampler.h`, `fwbuild/glue` | contributed under GPL-3.0-only here | MwModuleApi, MwTrackSlot, MwService, glue |
| DotGothic16 (The DotGothic16 Project Authors) | `app/src/main/res/font/dotgothic16.ttf` (printable-ASCII subset, converted from Felucca `web/emu/fonts`) | OFL-1.1 | panel knob labels, as in the browser emulator; licence text `LICENSES/OFL-DotGothic16.txt` |
| Ableton Link (Ableton AG) | `third_party/link` (submodule, tag Link-4.1) | GPL-2.0-or-later (used under GPL-3.0) | tempo / beat / start-stop sync over the LAN, docs/LINK.md |
| asio standalone (Christopher M. Kohlhoff) | `third_party/link/modules/asio-standalone` (Link's submodule) | BSL-1.0 | Link's networking |
| Oboe (Google) | Gradle prefab `com.google.oboe:oboe` | Apache-2.0 | audio I/O |
| Jetpack Compose, AndroidX | Gradle | Apache-2.0 | UI |
| HIIR (Laurent de Soras) | not used (P6 wrote its own half-band FIR oversampler, MASTER-BUS.md §8a) | WTFPL | — |
| WAMR — WebAssembly Micro Runtime (Bytecode Alliance) | `app/src/main/cpp/third_party/wamr` (P7) | Apache-2.0 WITH LLVM-exception | WASM firmware backend |
| Monocypher (Loup Vaillant) | `app/src/main/cpp/third_party/monocypher` (P3) | CC0-1.0 OR BSD-2-Clause | Ed25519 / SHA-512 verification |
| org.json | host tests only | JSON License (public-domain-like) | test scope |

Not used: M-VAVE branding, logos, fonts, trade dress; JieLi SDK blobs (needed only for real-device `.fwsc`).
