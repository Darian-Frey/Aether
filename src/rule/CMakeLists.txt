add_library(aether_rule STATIC
    dsl.cpp
    ir.cpp
    ir_json.cpp
    lut.cpp
    neighbourhood.cpp
    table_layout.cpp)

target_include_directories(aether_rule PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(aether_rule PUBLIC cxx_std_20)
target_compile_options(aether_rule PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether_rule PUBLIC nlohmann_json::nlohmann_json)
