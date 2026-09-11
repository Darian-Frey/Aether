# Third-party dependencies, fetched and built as part of the tree so that the
# GL backend raylib is compiled against is a property of this project rather
# than of whatever happens to be installed on the machine.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- raylib -------------------------------------------------------------------
# OPENGL_VERSION must be 4.3: raylib's default rlgl backend is 3.3, under which
# the compute shader entry points compile to no-ops (D-001).
set(OPENGL_VERSION "4.3" CACHE STRING "raylib OpenGL backend" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE)

# --- Dear ImGui ---------------------------------------------------------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7
    GIT_SHALLOW    TRUE)

# --- rlImGui ------------------------------------------------------------------
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        Raylib_6_0
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(raylib)
FetchContent_Populate(imgui)
FetchContent_Populate(rlimgui)

# Neither imgui nor rlImGui ships a CMake build; assemble them here.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_compile_features(imgui PUBLIC cxx_std_17)

add_library(rlimgui STATIC ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR})
target_link_libraries(rlimgui PUBLIC imgui raylib)
