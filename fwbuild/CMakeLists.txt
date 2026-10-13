# SPDX-License-Identifier: GPL-3.0-only
# FM1 DECK firmware modules: libfw_<id>.so, one per FM-1 firmware, built from the unmodified upstream sources
# (upstream/<id>, git submodules) + the emulator HAL + our glue (glue/, profiles/). See docs/FIRMWARE-ABI.md.
#
# Used two ways:
#   1. from the app (app/src/main/cpp/CMakeLists.txt, FM1DECK_BUILTIN_FIRMWARE=ON): built-in firmwares in the APK;
#   2. standalone with the NDK toolchain (tools/fwpack/build_pack.py, CI .github/workflows/firmware-packs.yml):
#        cmake -S fwbuild -B build/fw-arm64 -G Ninja \
#              -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a \
#              -DANDROID_PLATFORM=android-29 -DFM1DECK_FIRMWARES=jiant -DFM1DECK_GEN_ROOT=build/fwgen
#
# Generated headers (fonts, icons, tables, samples...) are NOT in the upstream repos: tools/fwgen/fwgen.py runs each
# upstream's own tools/build.py generate() step into ${FM1DECK_GEN_ROOT}/<id>/gen (see fwbuild/README.md).
cmake_minimum_required(VERSION 3.22)
project(fm1deck_fw C)

set(FM1DECK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "FM1 DECK repository root")
set(FM1DECK_UPSTREAM_ROOT "${FM1DECK_REPO_ROOT}/upstream" CACHE PATH "upstream/<id> checkouts (submodules)")
set(FM1DECK_GEN_ROOT "${FM1DECK_REPO_ROOT}/build/fwgen" CACHE PATH "generated headers: <root>/<id>/gen")
set(FM1DECK_FIRMWARES "felucca;jiant;sloop;hortator" CACHE STRING "firmware ids to build")
set(FM1DECK_FW_BUILD_INFO "local" CACHE STRING "build_info string baked into the modules")
set(FM1DECK_FW_API_DIR "${FM1DECK_REPO_ROOT}/app/src/main/cpp/fw")

# HAL per firmware: upstream's browser emulator where it exists, ours otherwise.
set(FM1DECK_HAL_felucca "${FM1DECK_UPSTREAM_ROOT}/felucca/web/emu/felucca_web.c")
set(FM1DECK_HAL_jiant   "${FM1DECK_UPSTREAM_ROOT}/jiant/web/emu/felucca_web.c")
set(FM1DECK_HAL_sloop   "${CMAKE_CURRENT_SOURCE_DIR}/hal/sloop/sloop_web.c")
set(FM1DECK_HAL_hortator "${CMAKE_CURRENT_SOURCE_DIR}/hal/hortator/hortator_web.c")
# Firmwares whose sources say "<prefix>DEV" and whose own build passes <prefix><VERSION.txt> (tools/build.py)
set(FM1DECK_VERSION_PREFIX_hortator "DRUM-")

function(fm1deck_firmware_version id out)
    set(src "${FM1DECK_UPSTREAM_ROOT}/${id}/firmware/src")
    set(ver "")
    if (DEFINED FM1DECK_VERSION_PREFIX_${id} AND EXISTS "${FM1DECK_UPSTREAM_ROOT}/${id}/VERSION.txt")
        file(READ "${FM1DECK_UPSTREAM_ROOT}/${id}/VERSION.txt" file_ver)
        string(STRIP "${file_ver}" file_ver)
        set(ver "${FM1DECK_VERSION_PREFIX_${id}}${file_ver}")
    endif()
    foreach(f felucca.c ui.c core.h)
        if (NOT ver AND EXISTS "${src}/${f}")
            file(STRINGS "${src}/${f}" line REGEX "^#define (FELUCCA|SLOOP)_VERSION \"")
            if (line)
                list(GET line 0 line)
                string(REGEX REPLACE "^#define [A-Z]+_VERSION \"([^\"]*)\".*" "\\1" ver "${line}")
            endif()
        endif()
    endforeach()
    if (NOT ver)
        set(ver "unknown")
    endif()
    set(${out} "${ver}" PARENT_SCOPE)
endfunction()

function(fm1deck_firmware_build_info id out)
    execute_process(
        COMMAND git -C "${FM1DECK_UPSTREAM_ROOT}/${id}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE up_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (NOT up_sha)
        set(up_sha "unknown")
    endif()
    if (FM1DECK_FW_BUILD_INFO)
        set(${out} "upstream=${up_sha} ${FM1DECK_FW_BUILD_INFO}" PARENT_SCOPE)
    else()
        execute_process(
            COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=12 HEAD:fwbuild
            OUTPUT_VARIABLE fw_sha
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if (NOT fw_sha)
            set(fw_sha "unknown")
        endif()
        set(${out} "upstream=${up_sha} fwbuild=${fw_sha}" PARENT_SCOPE)
    endif()
endfunction()

function(fm1deck_add_firmware id)
    set(hal "${FM1DECK_HAL_${id}}")
    set(gen "${FM1DECK_GEN_ROOT}/${id}/gen")
    if (NOT EXISTS "${hal}")
        message(FATAL_ERROR "fwbuild: no HAL for ${id} at ${hal}")
    endif()
    if (NOT EXISTS "${gen}")
        message(FATAL_ERROR "fwbuild: no generated headers for ${id} at ${gen} (run tools/fwgen/fwgen.py ${id})")
    endif()
    fm1deck_firmware_version(${id} ver)
    fm1deck_firmware_build_info(${id} binfo)
    add_library(fw_${id} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/glue/fm1deck_module.c")
    target_include_directories(fw_${id} PRIVATE
            "${gen}"
            "${FM1DECK_UPSTREAM_ROOT}/${id}/firmware/src"
            "${FM1DECK_FW_API_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/glue"
            "${CMAKE_CURRENT_SOURCE_DIR}/profiles")
    target_compile_definitions(fw_${id} PRIVATE
            "FM1DECK_HAL=\"${hal}\""
            "FM1DECK_PROFILE=\"${CMAKE_CURRENT_SOURCE_DIR}/profiles/${id}.h\""
            "FELUCCA_VERSION=\"${ver}\""
            "FM1DECK_BUILD_INFO=\"${binfo}\"")
    # Capability overrides from CI, e.g. -DFM1DECK_GLUE_FLAGS_jiant="FM1DECK_GLUE_UI_STATE=0"
    if (FM1DECK_GLUE_FLAGS_${id})
        target_compile_definitions(fw_${id} PRIVATE ${FM1DECK_GLUE_FLAGS_${id}})
    endif()
    # As the upstream host / emulator build: gnu99, warnings off, no fused multiply-adds (bit-identical to the
    # upstream WASM, the golden test relies on it). Optimised in debug APKs too: it renders in the audio callback.
    target_compile_options(fw_${id} PRIVATE -std=gnu99 -w -O2 -ffp-contract=off -fvisibility=hidden)
    target_link_options(fw_${id} PRIVATE -Wl,-Bsymbolic -Wl,-z,max-page-size=16384)
    target_link_libraries(fw_${id} m)
    set_target_properties(fw_${id} PROPERTIES OUTPUT_NAME "fw_${id}")
    message(STATUS "fwbuild: ${id} ${ver}")
endfunction()

foreach(id IN LISTS FM1DECK_FIRMWARES)
    fm1deck_add_firmware(${id})
endforeach()
