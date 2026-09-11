add_library(aether_sim STATIC
    cpu_step.cpp)

target_include_directories(aether_sim PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(aether_sim PUBLIC cxx_std_20)
target_compile_options(aether_sim PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether_sim PUBLIC aether_core aether_rule)
