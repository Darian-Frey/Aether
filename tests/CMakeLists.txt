include(Catch)

add_executable(aether_tests
    support/gl_context.cpp
    core/grid_test.cpp
    core/gpu_grid_test.cpp
    rule/neighbourhood_test.cpp
    rule/table_layout_test.cpp
    rule/ir_test.cpp
    rule/dsl_test.cpp
    rule/lut_test.cpp
    sim/boundary_test.cpp
    sim/cpu_step_test.cpp
    sim/equivalence_test.cpp
    sim/hash_test.cpp
    sim/scheduler_test.cpp
    sim/rng_test.cpp
    sim/simulation_test.cpp
    render/view2d_test.cpp
    render/renderer2d_test.cpp
    ui/brush_test.cpp
    sim/paint_test.cpp)

target_compile_options(aether_tests PRIVATE ${AETHER_WARNINGS})
target_include_directories(aether_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aether_tests PRIVATE aether_core aether_rule aether_sim aether_render aether_ui Catch2::Catch2WithMain)

catch_discover_tests(aether_tests)
