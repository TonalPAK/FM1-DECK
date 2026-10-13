cmake_minimum_required(VERSION 3.22)
project(fm1deck_render CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fm1deck_render fm1deck_render.cpp)
target_include_directories(fm1deck_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp/fw
)
target_link_libraries(fm1deck_render PRIVATE ${CMAKE_DL_LIBS})
