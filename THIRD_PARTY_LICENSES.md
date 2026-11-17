# Third-party components and licences

FM1 DECK is licensed **GPL-3.0-only** (it executes and distributes GPL-3.0-only firmwares). Every component below is
compatible with GPL-3.0. Keep this file current: a phase that adds code from elsewhere adds a row here.

| Component | Where | Licence | Notes |
|---|---|---|---|
| Felucca (Leo Kuroshita / Hügelton Instruments) | `upstream/felucca` (1.8.2), firmware packs | GPL-3.0-only | unmodified; includes OFL fonts, msfa (Apache-2.0), Fukiai font (MIT) — see its `LICENSING.md` |
| JIANT FM (juanjiant-bit) | `upstream/jiant` (V10.9), packs | GPL-3.0-only | fork of Felucca; song mode from SLOOP |
| SLOOP (isod89) | `upstream/sloop` (2.8.5), packs | GPL-3.0-only | based on Felucca |
| Hortator (DEADACTIVE) | `upstream/hortator` (1.0.1), packs | GPL-3.0-only | drum machine based on Felucca, unmodified; includes Terminus and UnifrakturMaguntia fonts (OFL-1.1), Grids port (GPL-3.0-or-later), Plaits / Streams ports (MIT), the Felucca Assets under its GPL §7 permission — see its `LICENSING.md` |
| Mutable Dreams (Kid Bunka) | `upstream/mutable` (2.3) | GPL-3.0-only | built on `fm1-base`; includes Mutable Instruments Plaits/Rings/Elements DSP (MIT) |
| FMini-1 (Ruppert) | `upstream/fmini` (2.6.0) | GPL-3.0-only | built on Felucca |
| OberVave (zvenson) | `upstream/obervave` (3.5.2) | GPL-3.0-only | built on Dinghy |
| CoralFlower (FicusFicusov) | `upstream/coralflower` (0.7.1) | GPL-3.0-only | built on Felucca |
| AMB-1 (Charles Vestal) | `upstream/amb` (1.3.2) | MPL-2.0 | built on `fm1-base` |
| Blop (Obsimo / OSR-Experiment) | `upstream/blop` (1.1.0) | GPL-3.0-only | built on `fm1-base` |
| LOAF BREAKER (FLEXOR) | `upstream/loafbreaker` (0.9.66) | MPL-2.0 | built on CHOPPAH / `fm1-base` |
| FRASIL (Styves Goupil) | `upstream/frasil` (3.3.1) | MPL-2.0 | built on `fm1-base` |
| ATEK303 (Animatek) | `upstream/atek303` (1.2) | GPL-3.0-or-later | Open303 engine on `fm1-base` |
| Guru-600 (Marc Massé) | `upstream/guru600` (1.3.0) | GPL-3.0-only | built on Felucca |
| Ravel (De-Pattern) | `upstream/ravel` (1.42.0) | GPL-3.0-only | 8-track groovebox |
| S1Vave (Andrew GG) | `upstream/s1vave` (2.0.1) | GPL-3.0-only | built on Dinghy / OberVave |
| Clipper (isod89) | `upstream/clipper` (1.3.1) | GPL-3.0-only | built on SLOOP |
| JAMTRACK (PhilipsKnobs) | `upstream/jamtrack` (0.2.3) | MIT | built on `fm1-base` |
| FiMba-1 (jadamsowers) | `upstream/fimba` (0.7.0) | GPL-3.0-only | physically modelled kalimba |
| AISLING (CU_STE) | `upstream/aisling` (1.4.1) | GPL-3.0-only | generative Irish ambient |
| ECHOFIELD (PhilipsKnobs) | `upstream/echofield` (0.2.13) | MIT | built on `fm1-base` |
| Arctic Scream (PolarPatch) | `upstream/arctic` (2.1.1) | GPL-3.0-only | built on Dinghy |
| LOOM-8 (Charles Vestal) | `upstream/loom` (0.16.2) | MPL-2.0 | built on `fm1-base` |
| Demake (Sequencist) | `upstream/demake` (0.4.25) | GPL-3.0-only | ambient lofi machine |
| K-WASUB (Omnipotard / Guizmoh) | `upstream/kwasub` (1.1.03) | GPL-3.0-only | bass synth |
| Shine On (zvenson) | `upstream/shineon` (3.5) | GPL-3.0-only | built on OberVave / Dinghy |
| CHOPPAH (Laurens Profittlich) | `upstream/choppah` (1.4.1) | MPL-2.0 | built on `fm1-base` |
| SHRUB (Steve Duda / Xfer Records) | `fwbuild/hal/shrub/shrub_web.c` + `tools/shrub_wasm2c.py` (1.05) | Freeware / proprietary Xfer WASM translated locally | extracted from user-supplied `Shrub.vst3` (`shrub.wasm`) for local evaluation |
| Firmware Hub thumbnails (Sven Trogus & firmware authors) | `app/src/main/assets/fw_thumbs/` | GPL-3.0-only | card artwork from `thefirmwarehub.com` |
| Host HAL adapters (`fm1base`, `dinghy`, `aisling`, `fimba`, `coralflower`, `arctic`, `kwasub`, `demake`, `ravel`) | `fwbuild/hal/` | GPL-3.0-only | wraps unmodified upstream firmware simulators into `Fm1FirmwareApi` |
| Browser-emulator HAL design (X0X by charlesvestal, Felucca `web/emu`) | used via upstream `felucca_web.c` | GPL-3.0 | not copied, built from upstream |
| SLOOP host HAL `fwbuild/hal/sloop/sloop_web.c` | fwbuild | GPL-3.0-only | first written for GGBOX (an earlier port by the same author), adapting Felucca's `felucca_web.c` |
| Hortator host HAL `fwbuild/hal/hortator/hortator_web.c` | fwbuild | GPL-3.0-only | wraps Hortator's own web simulator core (`tests/sim_core.c`, built from upstream, not copied) |
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
