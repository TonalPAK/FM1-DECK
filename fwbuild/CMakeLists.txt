# SPDX-License-Identifier: GPL-3.0-only
# FM1 DECK firmware modules: libfw_<id>.so, one per FM-1 firmware, built from the unmodified upstream sources
# (upstream/<id>) + the emulator HAL + our glue (glue/, profiles/). See docs/FIRMWARE-ABI.md.
cmake_minimum_required(VERSION 3.22)
project(fm1deck_fw C CXX)

set(FM1DECK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "FM1 DECK repository root")
set(FM1DECK_UPSTREAM_ROOT "${FM1DECK_REPO_ROOT}/upstream" CACHE PATH "upstream/<id> checkouts")
set(FM1DECK_GEN_ROOT "${FM1DECK_REPO_ROOT}/build/fwgen" CACHE PATH "generated headers: <root>/<id>/gen")
set(FM1DECK_FIRMWARES
    "felucca;jiant;sloop;hortator;mutable;fmini;obervave;coralflower;amb;blop;loafbreaker;frasil;atek303;guru600;ravel;s1vave;clipper;jamtrack;fimba;aisling;echofield;arctic;loom;demake;kwasub;shineon;choppah"
    CACHE STRING "firmware ids to build")
set(FM1DECK_FW_BUILD_INFO "local" CACHE STRING "build_info string baked into the modules")
set(FM1DECK_FW_API_DIR "${FM1DECK_REPO_ROOT}/app/src/main/cpp/fw")

# Versions when not defined via FELUCCA_VERSION in source
set(FM1DECK_VERSION_PREFIX_hortator "DRUM-")

function(fm1deck_firmware_version id out)
    set(src "${FM1DECK_UPSTREAM_ROOT}/${id}/firmware/src")
    set(ver "")
    if (DEFINED FM1DECK_VERSION_PREFIX_${id} AND EXISTS "${FM1DECK_UPSTREAM_ROOT}/${id}/VERSION.txt")
        file(READ "${FM1DECK_UPSTREAM_ROOT}/${id}/VERSION.txt" file_ver)
        string(STRIP "${file_ver}" file_ver)
        set(ver "${FM1DECK_VERSION_PREFIX_${id}}${file_ver}")
    endif()
    foreach(f felucca.c ui.c core.h app.h)
        if (NOT ver AND EXISTS "${src}/${f}")
            file(STRINGS "${src}/${f}" line REGEX "^#define (FELUCCA|SLOOP|APP)_VER(SION)? \"")
            if (line)
                list(GET line 0 line)
                string(REGEX REPLACE "^#define [A-Z0-9_]+ \"([^\"]*)\".*" "\\1" ver "${line}")
            endif()
        endif()
    endforeach()
    if (NOT ver)
        set(ver "1.0")
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

function(fm1deck_add_fm1base_firmware id app_dir base_dir)
    set(gen "${FM1DECK_GEN_ROOT}/${id}/gen")
    if (NOT EXISTS "${gen}")
        message(FATAL_ERROR "fwbuild: no generated headers for ${id} at ${gen}")
    endif()

    set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/fm1base/fm1base_web.c")
    set(extra_srcs "")
    set(extra_incs "")
    set(extra_defs -DPLAT_HOST=1 -DPLAT_WEB=1)
    set(o2_defs "")

    if (id STREQUAL "amb")
        set(extra_srcs "${app_dir}/dsp/amb.c")
        set(extra_incs "${app_dir}/dsp")
    elseif (id STREQUAL "blop")
        set(extra_srcs "${app_dir}/dsp/engine.c")
        set(extra_incs "${app_dir}/dsp")
        list(APPEND extra_defs -DFM1BASE_NO_LIGHTS_GLOW=1)
    elseif (id STREQUAL "choppah")
        file(GLOB extra_srcs "${app_dir}/dsp/*.c")
        set(extra_incs "${app_dir}/dsp")
        list(APPEND extra_defs -DFM1B_BLE=1)
    elseif (id STREQUAL "echofield")
        set(extra_srcs "${app_dir}/dsp/engine.c")
        set(extra_incs "${app_dir}/dsp")
        list(APPEND extra_defs -DFM1B_MIRROR=1 -DFM1B_BACKUP=1 -DFM1B_CPU2=1)
    elseif (id STREQUAL "frasil")
        file(GLOB extra_srcs "${app_dir}/../engine/dsp/*.c")
        set(extra_incs "${app_dir}/../engine" "${app_dir}/../engine/dsp")
        list(APPEND extra_defs -DFM1B_BACKUP=1)
        set(o2_defs FRS_LEAD=1)
    elseif (id STREQUAL "jamtrack")
        set(extra_srcs "${app_dir}/dsp/engine.c")
        set(extra_incs "${app_dir}/dsp")
        list(APPEND extra_defs -DFM1B_MIRROR=1 -DFM1B_BACKUP=1)
    elseif (id STREQUAL "loafbreaker")
        file(GLOB extra_srcs "${app_dir}/dsp/*.c")
        set(extra_incs "${app_dir}/dsp")
        list(APPEND extra_defs -DFM1B_MIRROR=1)
    elseif (id STREQUAL "loom")
        set(extra_srcs
            "${app_dir}/dsp/synth.c" "${app_dir}/dsp/drumline.c" "${app_dir}/dsp/digisophie.c"
            "${app_dir}/dsp/sample.c" "${app_dir}/dsp/fx.c" "${app_dir}/dsp/bass303.c"
            "${app_dir}/dsp/d909.c" "${app_dir}/dsp/bench.c" "${app_dir}/seq/pattern.c" "${app_dir}/seq/seq.c")
        list(APPEND extra_defs -DFM1B_MIRROR=1 -DFM1B_BACKUP=1 -DFM1B_CPU2=1 -DFM1B_USB_HOST=1 -DFM1B_RES=1 -DFM1B_BLE=1 -DFM1B_BLE_CENTRAL=1 -DFM1B_UAC_CH=10)
    elseif (id STREQUAL "mutable")
        set(extra_srcs
            "${app_dir}/third_party/elements/dsp/exciter.cc"
            "${app_dir}/third_party/elements/dsp/resonator.cc"
            "${app_dir}/third_party/elements/resources.cc"
            "${app_dir}/third_party/plaits/dsp/engine/additive_engine.cc"
            "${app_dir}/third_party/plaits/dsp/engine/fm_engine.cc"
            "${app_dir}/third_party/plaits/dsp/engine/virtual_analog_engine.cc"
            "${app_dir}/third_party/plaits/resources.cc"
            "${app_dir}/third_party/rings/dsp/resonator.cc"
            "${app_dir}/third_party/rings/resources.cc"
            "${app_dir}/third_party/stmlib/dsp/units.cc"
            "${app_dir}/third_party/stmlib/utils/random.cc"
            "${app_dir}/dsp/dreams.cpp")
        set(extra_incs "${app_dir}/third_party")
        list(APPEND extra_defs -DFM1B_MIRROR=1 -DFM1B_BACKUP=1 -DFM1B_BLE=1 -DFM1B_BLE_CENTRAL=1 -DFM1B_UAC_CH=8)
        set(o2_defs TEST=1 MD_DSP_RAM=1)
    elseif (id STREQUAL "atek303")
        set(extra_srcs "${app_dir}/dsp/atek_voice.c" "${app_dir}/dsp/acid_pattern.c" "${app_dir}/dsp/fx.c")
        set(extra_incs "${app_dir}/dsp")
        list(APPEND extra_defs -DFM1B_BACKUP=1)
    endif()

    fm1deck_firmware_build_info(${id} binfo)
    add_library(fw_${id} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/glue/fm1deck_module.c" ${extra_srcs})
    target_include_directories(fw_${id} PRIVATE
        "${gen}"
        "${base_dir}/host"
        "${base_dir}/device"
        "${base_dir}/include"
        "${base_dir}/lib"
        "${app_dir}"
        ${extra_incs}
        "${FM1DECK_FW_API_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/glue"
        "${CMAKE_CURRENT_SOURCE_DIR}/profiles")
    target_compile_definitions(fw_${id} PRIVATE
        "FM1DECK_HAL=\"${hal}\""
        "FM1BASE_EMU_WEB_C=\"${base_dir}/web/emu/emu_web.c\""
        "FM1DECK_PROFILE=\"${CMAKE_CURRENT_SOURCE_DIR}/profiles/${id}.h\""
        "FELUCCA_VERSION=\"1.0\""
        "FM1DECK_BUILD_INFO=\"${binfo}\""
        ${extra_defs})
    if (o2_defs)
        set_source_files_properties(${extra_srcs} PROPERTIES COMPILE_DEFINITIONS "${o2_defs}")
    endif()
    foreach(src_file IN LISTS extra_srcs)
        if (src_file MATCHES "\\.c$")
            get_filename_component(src_dir "${src_file}" DIRECTORY)
            set_source_files_properties("${src_file}" PROPERTIES INCLUDE_DIRECTORIES "${src_dir}")
        endif()
    endforeach()
    target_compile_options(fw_${id} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++11 -fno-exceptions -fno-rtti>
        -w -O2 -ffp-contract=off -fvisibility=hidden)
    target_link_options(fw_${id} PRIVATE -Wl,-Bsymbolic -Wl,-z,max-page-size=16384)
    target_link_libraries(fw_${id} m)
    set_target_properties(fw_${id} PROPERTIES OUTPUT_NAME "fw_${id}")
    message(STATUS "fwbuild: ${id} (fm1-base)")
endfunction()

function(fm1deck_add_firmware id)
    if (id STREQUAL "blop")
        fm1deck_add_fm1base_firmware(${id} "${FM1DECK_UPSTREAM_ROOT}/blop" "${FM1DECK_UPSTREAM_ROOT}/blop/fm1-base")
        return()
    elseif (id STREQUAL "atek303")
        fm1deck_add_fm1base_firmware(${id} "${FM1DECK_UPSTREAM_ROOT}/atek303/atek303/app" "${FM1DECK_UPSTREAM_ROOT}/amb/base")
        return()
    elseif (id MATCHES "^(amb|choppah|echofield|frasil|jamtrack|loafbreaker|loom|mutable)$")
        fm1deck_add_fm1base_firmware(${id} "${FM1DECK_UPSTREAM_ROOT}/${id}/app" "${FM1DECK_UPSTREAM_ROOT}/${id}/base")
        return()
    endif()

    set(gen "${FM1DECK_GEN_ROOT}/${id}/gen")
    if (NOT EXISTS "${gen}")
        message(FATAL_ERROR "fwbuild: no generated headers for ${id} at ${gen} (run tools/fwgen/fwgen.py ${id})")
    endif()

    set(up "${FM1DECK_UPSTREAM_ROOT}/${id}")
    set(extra_srcs "")
    set(extra_incs "")
    set(extra_defs "")

    if (id STREQUAL "felucca")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/felucca/felucca_web.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/hal" "${up}/firmware/bsp/chip" "${up}/firmware/bsp/boards/fm1")
    elseif (id STREQUAL "jiant" OR id STREQUAL "fmini")
        set(hal "${up}/web/emu/felucca_web.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/hal" "${up}/firmware/bsp/chip" "${up}/firmware/bsp/boards/fm1")
    elseif (id STREQUAL "guru600")
        set(hal "${up}/firmware/web/emu/felucca_web.c")
        set(extra_incs "${up}/firmware/firmware/src" "${up}/firmware/firmware/hal" "${up}/firmware/firmware/bsp/chip" "${up}/firmware/firmware/bsp/boards/fm1")
    elseif (id STREQUAL "sloop" OR id STREQUAL "clipper")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/sloop/sloop_web.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/hal" "${up}/firmware/bsp/chip" "${up}/firmware/bsp/boards/fm1")
    elseif (id STREQUAL "hortator")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/hortator/hortator_web.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/hal" "${up}/firmware/bsp/chip" "${up}/firmware/bsp/boards/fm1")
    elseif (id STREQUAL "coralflower")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/coralflower/coralflower_web.c")
        set(extra_incs "${up}/tools/emu/web/compat" "${up}/firmware/src" "${up}/firmware/hal")
        set(extra_defs -DEMU_WEB=1)
    elseif (id STREQUAL "aisling")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/aisling/aisling_web.c")
        set(extra_srcs "${up}/firmware/src/dsp/amb.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/src/dsp" "${up}/firmware/hal")
        set(extra_defs -DEN_HOST=1 -DEN_WEB=1)
    elseif (id STREQUAL "fimba")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/fimba/fimba_web.c")
        set(extra_srcs "${up}/firmware/src/dsp/kalimba.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/src/dsp" "${up}/firmware/hal")
        set(extra_defs -DOM_HOST=1 -DOM_WEB=1)
    elseif (id STREQUAL "arctic")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/arctic/arctic_web.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/hal")
    elseif (id STREQUAL "kwasub")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/kwasub/kwasub_web.c")
        set(extra_incs "${up}/source/firmware/src" "${up}/source/firmware/hal")
    elseif (id STREQUAL "demake")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/demake/demake_web.c")
        set(extra_incs "${up}/firmware/src")
    elseif (id STREQUAL "ravel")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/ravel/ravel_web.c")
        set(extra_srcs "${up}/firmware/app/ravel_app.c")
        set(extra_incs "${up}/firmware/app")
    elseif (id MATCHES "^(obervave|s1vave|shineon)$")
        set(hal "${CMAKE_CURRENT_SOURCE_DIR}/hal/dinghy/dinghy_web.c")
        set(extra_incs "${up}/firmware/src" "${up}/firmware/hal")
        set(extra_defs "DINGHY_APP_C=\"../../firmware/src/${id}.c\"")
    else()
        message(FATAL_ERROR "fwbuild: unknown firmware id ${id}")
    endif()

    if (NOT EXISTS "${hal}")
        message(FATAL_ERROR "fwbuild: no HAL for ${id} at ${hal}")
    endif()

    fm1deck_firmware_version(${id} ver)
    fm1deck_firmware_build_info(${id} binfo)
    add_library(fw_${id} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/glue/fm1deck_module.c" ${extra_srcs})
    target_include_directories(fw_${id} PRIVATE
        "${gen}"
        ${extra_incs}
        "${FM1DECK_FW_API_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/glue"
        "${CMAKE_CURRENT_SOURCE_DIR}/profiles")
    target_compile_definitions(fw_${id} PRIVATE
        "FM1DECK_HAL=\"${hal}\""
        "FM1DECK_PROFILE=\"${CMAKE_CURRENT_SOURCE_DIR}/profiles/${id}.h\""
        "FELUCCA_VERSION=\"${ver}\""
        "FM1DECK_BUILD_INFO=\"${binfo}\""
        ${extra_defs})
    if (FM1DECK_GLUE_FLAGS_${id})
        target_compile_definitions(fw_${id} PRIVATE ${FM1DECK_GLUE_FLAGS_${id}})
    endif()
    target_compile_options(fw_${id} PRIVATE -std=gnu99 -w -O2 -ffp-contract=off -fvisibility=hidden)
    target_link_options(fw_${id} PRIVATE -Wl,-Bsymbolic -Wl,-z,max-page-size=16384)
    target_link_libraries(fw_${id} m)
    set_target_properties(fw_${id} PROPERTIES OUTPUT_NAME "fw_${id}")
    message(STATUS "fwbuild: ${id} ${ver}")
endfunction()

foreach(id IN LISTS FM1DECK_FIRMWARES)
    fm1deck_add_firmware(${id})
endforeach()
