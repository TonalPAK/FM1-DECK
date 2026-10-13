# FM1 DECK native: libfm1deck.so (Oboe engine, firmware host, DSP, JNI) and, optionally, the built-in firmware
# modules libfw_<id>.so (fwbuild/). Layout and rules: docs/ARCHITECTURE.md, CONTRIBUTING.md.
cmake_minimum_required(VERSION 3.22.1)
project(fm1deck C CXX)

option(FM1DECK_BUILTIN_FIRMWARE "Build fwbuild/ firmware modules into the APK (needs build/fwgen/<id>/gen)" OFF)
option(FM1DECK_DSP_PROFILE "DSP stage profiler on from the start, logcat tag FM1_PROF" OFF)
option(FM1DECK_WASM "WAMR WebAssembly firmware backend (phase P7)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(oboe REQUIRED CONFIG)

# Sources are picked up by folder so phases can add files without touching this list.
file(GLOB_RECURSE FM1DECK_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/jni/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/fw/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dsp/*.cpp)

add_library(fm1deck SHARED ${FM1DECK_SOURCES})
target_include_directories(fm1deck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# No global -ffast-math (denormals / NaN behaviour must stay defined, CONTRIBUTING.md). Debug APKs still get -O2:
# the DSP runs in the audio callback and must be measured as it ships.
target_compile_options(fm1deck PRIVATE -Wall -Wextra $<$<CONFIG:Debug>:-O2>)
if (FM1DECK_DSP_PROFILE)
    target_compile_definitions(fm1deck PRIVATE FM1DECK_DSP_PROFILE=1)
endif()
# Ableton Link (GPL-2.0-or-later; docs/LINK.md): header-only with standalone asio. Asio needs exceptions and RTTI,
# which the NDK enables by default.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/link/AbletonLinkConfig.cmake)

target_link_libraries(fm1deck PRIVATE oboe::oboe Ableton::Link android log dl jnigraphics)
target_link_options(fm1deck PRIVATE -Wl,-z,max-page-size=16384)

if (FM1DECK_BUILTIN_FIRMWARE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../fwbuild ${CMAKE_CURRENT_BINARY_DIR}/fwbuild)
endif()
