# Shader sources under shaders/ are embedded as string constants so the
# binary carries them; the files remain the versioned source.
set(AETHER_SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(AETHER_SHADER_GEN ${CMAKE_BINARY_DIR}/generated/shaders)
file(MAKE_DIRECTORY ${AETHER_SHADER_GEN})

function(aether_embed_shader SHADER SYMBOL OUT_VAR)
    set(out ${AETHER_SHADER_GEN}/${SYMBOL}.cpp)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${AETHER_SHADER_DIR}/${SHADER}
            -DOUTPUT=${out}
            -DSYMBOL=${SYMBOL}
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedShader.cmake
        DEPENDS ${AETHER_SHADER_DIR}/${SHADER} ${CMAKE_SOURCE_DIR}/cmake/EmbedShader.cmake
        COMMENT "Embedding shaders/${SHADER}"
        VERBATIM)
    set(${OUT_VAR} ${out} PARENT_SCOPE)
endfunction()

aether_embed_shader(lut_step.comp kLutStepComp LUT_STEP_CPP)

add_library(aether_sim STATIC
    cpu_step.cpp
    fill.cpp
    gpu_step.cpp
    simulation.cpp
    ${LUT_STEP_CPP})

target_include_directories(aether_sim PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(aether_sim PUBLIC cxx_std_20)
target_compile_options(aether_sim PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether_sim PUBLIC aether_core aether_rule raylib)
