aether_embed_shader(hash.glsl kHashGlsl HASH_GLSL_CPP)
aether_embed_shader(lut_step.comp kLutStepComp LUT_STEP_CPP)
aether_embed_shader(continuous_step.comp kContinuousStepComp CONTINUOUS_STEP_CPP)

add_library(aether_sim STATIC
    cpu_step.cpp
    fill.cpp
    gpu_step.cpp
    lineage.cpp
    pattern.cpp
    pattern_library.cpp
    rule_mutation.cpp
    session.cpp
    simulation.cpp
    ${HASH_GLSL_CPP}
    ${LUT_STEP_CPP}
    ${CONTINUOUS_STEP_CPP})

target_include_directories(aether_sim PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(aether_sim PUBLIC cxx_std_20)
target_compile_options(aether_sim PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether_sim PUBLIC aether_core aether_rule raylib)
