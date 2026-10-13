cmake_minimum_required(VERSION 3.22)
project(fm1deck_native_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp
)

add_compile_options(-Wall -Wextra -O2)

option(FM1DECK_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(FM1DECK_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# test_slot_gating
add_executable(test_slot_gating test_slot_gating.cpp)
target_link_libraries(test_slot_gating pthread)

# test_slot_park_stress
add_executable(test_slot_park_stress test_slot_park_stress.cpp)
target_link_libraries(test_slot_park_stress pthread)

# test_resampler
add_executable(test_resampler test_resampler.cpp)
target_link_libraries(test_resampler pthread)

# test_master_bus (docs/MASTER-BUS.md §8)
add_executable(test_master_bus test_master_bus.cpp)
target_link_libraries(test_master_bus pthread)

# test_clock_pulser (docs/LINK.md)
add_executable(test_clock_pulser test_clock_pulser.cpp)

enable_testing()
add_test(NAME test_slot_gating COMMAND test_slot_gating)
add_test(NAME test_slot_park_stress COMMAND test_slot_park_stress)
add_test(NAME test_resampler COMMAND test_resampler)
add_test(NAME test_master_bus COMMAND test_master_bus)
add_test(NAME test_clock_pulser COMMAND test_clock_pulser)
