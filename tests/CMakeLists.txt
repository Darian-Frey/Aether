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
    rule/ir_json_test.cpp
    sim/boundary_test.cpp
    sim/cpu_step_test.cpp
    sim/equivalence_test.cpp
    sim/hash_test.cpp
    sim/rule_mutation_test.cpp
    sim/lineage_test.cpp
    sim/session_test.cpp
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

# AV-006 in its literal form: one process records a session under both
# mutations, a second replays it from the initial state on each path, a third
# compares. Skipped (exit 77) where no GL context can be made.
set(AETHER_REPLAY_DIR ${CMAKE_BINARY_DIR}/replay_test)
file(MAKE_DIRECTORY ${AETHER_REPLAY_DIR})
add_test(NAME replay.record
    COMMAND aether headless --rule B3/S23 --size 128x96 --seed 5 --seed-b 6
            --rule-mutation 100:1 --cell-mutation 0.001 --generations 5000
            --save ${AETHER_REPLAY_DIR}/a.aether)
add_test(NAME replay.gpu
    COMMAND aether replay ${AETHER_REPLAY_DIR}/a.aether ${AETHER_REPLAY_DIR}/b.aether)
add_test(NAME replay.cpu
    COMMAND aether replay ${AETHER_REPLAY_DIR}/a.aether ${AETHER_REPLAY_DIR}/c.aether --cpu)
add_test(NAME replay.compare_gpu COMMAND aether compare ${AETHER_REPLAY_DIR}/a.aether ${AETHER_REPLAY_DIR}/b.aether)
add_test(NAME replay.compare_cpu COMMAND aether compare ${AETHER_REPLAY_DIR}/a.aether ${AETHER_REPLAY_DIR}/c.aether)
set_tests_properties(replay.record replay.gpu replay.cpu PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(replay.gpu replay.cpu PROPERTIES DEPENDS replay.record)
set_tests_properties(replay.compare_gpu PROPERTIES DEPENDS replay.gpu)
set_tests_properties(replay.compare_cpu PROPERTIES DEPENDS replay.cpu)
