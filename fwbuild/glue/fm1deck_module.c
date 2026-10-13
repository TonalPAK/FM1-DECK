/* SPDX-License-Identifier: GPL-3.0-only
 * FM1 DECK firmware module: one translation unit = the firmware's emulator HAL (which #includes the unmodified
 * firmware sources), then the profile, then the glue. CMake (fwbuild/CMakeLists.txt) passes:
 *   -DFM1DECK_HAL="<abs path to felucca_web.c | sloop_web.c>"  -DFM1DECK_PROFILE="<abs path to profiles/<id>.h>"
 *   -DFELUCCA_VERSION="..." -DFM1DECK_BUILD_INFO="..."
 *   -I<upstream>/firmware/src -I<gen dir> -I<app/src/main/cpp/fw> -I<fwbuild/glue>
 * Build flags as the upstream host build: -std=gnu99 -w -O2 -ffp-contract=off -fvisibility=hidden, -Wl,-Bsymbolic.
 * -ffp-contract=off keeps the output bit-identical to the upstream WASM build (golden test, docs/TESTING.md). */
#include FM1DECK_HAL
#include "Fm1FirmwareApi.h"                       /* before the profile: its ui_state uses Fm1UiState / FM1_LAYER_* */
#include FM1DECK_PROFILE
#include "fm1deck_glue.h"
