#include "rule/dsl.hpp"
#include "rule/compile.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::rule;

TEST_CASE("backend selection follows table size and form", "[lut]") {
    CHECK(selectBackend(*parseDsl("B3/S23").ir) == Backend::Lut);
    CHECK(selectBackend(*parseDsl("B2/S/C3").ir) == Backend::Lut);
    CHECK(selectBackend(*parseDsl("B2/S/C25").ir) == Backend::Lut);        // counted, so it fits (D-016)
    CHECK(selectBackend(*parseDsl("states 16; neighbourhood moore 1; 0: n(1) == 3 and n(2) == 0 -> 1; 1: n(1) < 2 -> 2;").ir) == Backend::Codegen);   // expression form

    RuleIR big;
    big.dimensions = 3;
    big.states = 2;
    big.kind = Kind::NonTotalistic;
    big.neighbourhood = {NeighbourhoodType::Moore, 1};
    Expression e;
    e.nodes = {{ExprOp::Self}};
    big.transition = e;
    CHECK(selectBackend(big) == Backend::Codegen);
}

TEST_CASE("compileRule carries everything the steppers need", "[lut]") {
    const auto ir = *parseDsl("B3/S23").ir;
    auto made = compileRule(ir);
    REQUIRE(std::holds_alternative<CompiledRule>(made));
    const CompiledRule& r = std::get<CompiledRule>(made);
    CHECK(r.ir_hash == irHash(ir));
    CHECK(r.states == 2);
    CHECK(r.kind == Kind::OuterTotalistic);
    CHECK(r.neighbourCount() == 8);
    CHECK(r.offsets == neighbourOffsets(2, {NeighbourhoodType::Moore, 1}));
    CHECK(r.table.size() == 18);
    CHECK(r.layout.size() == 18);
    REQUIRE(r.aux.size() == 9 * 2);
    CHECK(r.aux[8 * 2 + 1] == 9);   // W(8, 1) = 9
}

TEST_CASE("compileRule carries the compositions a full count vector needs", "[lut]") {
    // A rule whose own state asks about two different counts keeps the full
    // vector, so the auxiliary buffer is the W table.
    const auto ir = *parseDsl("states 4; neighbourhood moore 1; 0: n(1) == 1 and n(2) == 1 -> 1;").ir;
    REQUIRE(ir.kind == Kind::OuterTotalistic);
    auto made = compileRule(ir);
    REQUIRE(std::holds_alternative<CompiledRule>(made));
    const CompiledRule& r = std::get<CompiledRule>(made);
    const TableLayout L(Kind::OuterTotalistic, 4, 8);
    REQUIRE(r.aux.size() == 9 * 4);
    for (uint32_t n = 0; n <= 8; ++n)
        for (uint32_t m = 0; m < 4; ++m)
            CHECK(r.aux[n * 4 + m] == L.compositions(n, m));
    CHECK(r.aux[8 * 4 + 3] == 165);
}

TEST_CASE("compileRule carries the counted sets as masks", "[lut]") {
    const auto ir = *parseDsl("B2/S/C4").ir;
    REQUIRE(ir.kind == Kind::CountedTotalistic);
    auto made = compileRule(ir);
    REQUIRE(std::holds_alternative<CompiledRule>(made));
    const CompiledRule& r = std::get<CompiledRule>(made);
    CHECK(r.table.size() == 4 * 9);
    REQUIRE(r.aux.size() == 4 * 8);        // eight words of mask per own state
    CHECK(r.aux[0 * 8] == 0b10u);          // state 0 counts state 1
    CHECK(r.aux[1 * 8] == 0b10u);
    CHECK(r.aux[2 * 8] == 0u);             // the tail counts nothing
    CHECK(r.aux[3 * 8] == 0u);
    CHECK(r.counted.size() == 4);
    CHECK(r.counted[0].test(1));
    CHECK_FALSE(r.counted[0].test(2));
}

TEST_CASE("an expression rule compiles through codegen", "[lut]") {
    const auto ir = *parseDsl("states 16; neighbourhood moore 1; 0: n(1) == 3 and n(2) == 0 -> 1; 1: n(1) < 2 -> 2;").ir;
    REQUIRE(ir.kind == Kind::Expression);
    auto made = compileRule(ir);
    REQUIRE(std::holds_alternative<CompiledRule>(made));
    const CompiledRule& r = std::get<CompiledRule>(made);
    CHECK(r.backend == Backend::Codegen);
    CHECK(r.table.empty());                 // there is no table to hold
    CHECK(r.aux.empty());
    CHECK(r.expression.nodes.size() == std::get<Expression>(ir.transition).nodes.size());
    CHECK(r.expressionTypes.size() == r.expression.nodes.size());
    CHECK(r.glsl.find("uint aether_rule(uint self, uint nbr[8])") != std::string::npos);
}

TEST_CASE("compileRule refuses what no backend can serve", "[lut]") {
    RuleIR kernel;
    kernel.cell_type = aether::core::CellType::F32;
    kernel.kind = Kind::Continuous;
    Kernel k;
    k.profile = {1.0f};
    k.growth.nodes = {{ExprOp::FloatLiteral, 0, 0, 0, 0, 0.5f}};
    kernel.transition = k;
    auto refused = compileRule(kernel);
    REQUIRE(std::holds_alternative<CompileError>(refused));
    CHECK(std::get<CompileError>(refused).message.find("Phase 5") != std::string::npos);

    auto ir = *parseDsl("B3/S23").ir;
    std::get<Table>(ir.transition).entries.pop_back();
    auto bad = compileRule(ir);
    REQUIRE(std::holds_alternative<CompileError>(bad));
    CHECK(std::get<CompileError>(bad).message.find("invalid IR") != std::string::npos);
}
