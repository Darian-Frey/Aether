add_library(aether_core STATIC
    grid.cpp
    gpu_grid.cpp)

target_include_directories(aether_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(aether_core PUBLIC cxx_std_20)
target_compile_options(aether_core PRIVATE ${AETHER_WARNINGS})

# gpu_grid reaches GL through core/gl.hpp, which is glad from raylib's tree;
# the function pointers themselves live in libraylib.
target_include_directories(aether_core PUBLIC ${raylib_SOURCE_DIR}/src)
target_link_libraries(aether_core PUBLIC raylib)
