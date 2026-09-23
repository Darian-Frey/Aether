add_library(aether_ui STATIC
    app.cpp
    brush.cpp
    canvas.cpp
    capture.cpp
    editor.cpp
    headless.cpp
    panels.cpp)

target_include_directories(aether_ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(aether_ui PUBLIC cxx_std_20)
target_compile_options(aether_ui PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether_ui PUBLIC aether_core aether_rule aether_sim aether_render raylib rlimgui imgui)
