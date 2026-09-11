cmake_minimum_required(VERSION 3.20)

project(aether
    VERSION 0.0.1
    DESCRIPTION "Cellular automata laboratory"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(cmake/Dependencies.cmake)

add_executable(aether src/main.cpp)

target_compile_options(aether PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion>)

target_link_libraries(aether PRIVATE raylib rlimgui imgui)

# Direct access to the GL entry points raylib loads through glad. rlgl does
# not wrap glMemoryBarrier or the image-binding calls the ping-pong step
# needs, so the engine reaches past it here and nowhere else.
target_include_directories(aether PRIVATE ${raylib_SOURCE_DIR}/src)
