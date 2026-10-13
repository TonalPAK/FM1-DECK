#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi

PYTHON_CMD="python3"
if [ -f "$HOME/mwgen-venv/bin/python" ]; then
    PYTHON_CMD="$HOME/mwgen-venv/bin/python"
fi

cd "$REPO_ROOT"

echo "=== Building host firmware modules and wasm binaries ==="
cmake -S fwbuild -B build/fw-host -G Ninja >/dev/null
ninja -C build/fw-host >/dev/null

cmake -S tools/render -B build/tools-render -G Ninja >/dev/null
ninja -C build/tools-render >/dev/null

mkdir -p build/wasm
emcc -O2 -ffp-contract=off -std=gnu99 -w "-DFELUCCA_VERSION=\"v1.5.1\"" \
    -Ibuild/fwgen/felucca/gen -Iupstream/felucca/firmware/src \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/wasm/felucca.wasm upstream/felucca/web/emu/felucca_web.c

emcc -O2 -ffp-contract=off -std=gnu99 -w "-DFELUCCA_VERSION=\"v0.6.41\"" \
    -Ibuild/fwgen/jiant/gen -Iupstream/jiant/firmware/src \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/wasm/jiant.wasm upstream/jiant/web/emu/felucca_web.c

echo "=== Running Felucca Golden Test ==="
./build/tools-render/fm1deck_render tools/render/scripts/felucca-smoke.json build/fw-host/libfw_felucca.so build/felucca-native.raw
node tools/render/wasm_render.mjs tools/render/scripts/felucca-smoke.json build/wasm/felucca.wasm build/felucca-wasm.raw
$PYTHON_CMD tools/render/compare.py build/felucca-native.raw build/felucca-wasm.raw

echo "=== Running JIANT Golden Test ==="
./build/tools-render/fm1deck_render tools/render/scripts/jiant-smoke.json build/fw-host/libfw_jiant.so build/jiant-native.raw
node tools/render/wasm_render.mjs tools/render/scripts/jiant-smoke.json build/wasm/jiant.wasm build/jiant-wasm.raw
$PYTHON_CMD tools/render/compare.py build/jiant-native.raw build/jiant-wasm.raw

echo "=== All Golden Tests Passed! ==="
