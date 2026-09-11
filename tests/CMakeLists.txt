include(Catch)

add_executable(aether_tests
    rule/neighbourhood_test.cpp
    rule/table_layout_test.cpp
    rule/ir_test.cpp
    rule/dsl_test.cpp)

target_compile_options(aether_tests PRIVATE ${AETHER_WARNINGS})
target_link_libraries(aether_tests PRIVATE aether_rule Catch2::Catch2WithMain)

catch_discover_tests(aether_tests)
