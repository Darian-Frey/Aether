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

option(AETHER_BUILD_TESTS "Build the test suite" ON)

set(AETHER_WARNINGS
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion>)

include(cmake/Dependencies.cmake)

add_subdirectory(src/core)
add_subdirectory(src/rule)
add_subdirectory(src/sim)

add_executable(aether src/main.cpp)
target_compile_options(aether PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether PRIVATE aether_core aether_rule aether_sim raylib rlimgui imgui)

if(AETHER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
