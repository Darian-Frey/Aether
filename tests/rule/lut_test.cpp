#include "rule/dsl.hpp"
#include "rule/lut.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::rule;

TEST_CASE("backend selection follows table size and form", "[lut]") {
    CHECK(selectBackend(*parseDsl("B3/S23").ir) == Backend::Lut);
    CHECK(selectBackend(*parseDsl("B2/S/C3").ir) == Backend::Lut);
    CHECK(selectBackend(*parseDsl("B2/S/C25").ir) == Backend::Codegen);   // expression form

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

TEST_CASE("compileLut carries everything the steppers need", "[lut]") {
    const auto ir = *parseDsl("B3/S23").ir;
    auto made = compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(made));
    const LutRule& r = std::get<LutRule>(made);
    CHECK(r.ir_hash == irHash(ir));
    CHECK(r.states == 2);
    CHECK(r.kind == Kind::OuterTotalistic);
    CHECK(r.neighbourCount() == 8);
    CHECK(r.offsets == neighbourOffsets(2, {NeighbourhoodType::Moore, 1}));
    CHECK(r.table.size() == 18);
    CHECK(r.layout.size() == 18);
    REQUIRE(r.w.size() == 9 * 2);
    CHECK(r.w[8 * 2 + 1] == 9);   // W(8, 1) = 9
}

TEST_CASE("compileLut W table matches TableLayout for a multi-state rule", "[lut]") {
    const auto ir = *parseDsl("B2/S/C4").ir;
    auto made = compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(made));
    const LutRule& r = std::get<LutRule>(made);
    const TableLayout L(Kind::OuterTotalistic, 4, 8);
    REQUIRE(r.w.size() == 9 * 4);
    for (uint32_t n = 0; n <= 8; ++n)
        for (uint32_t m = 0; m < 4; ++m)
            CHECK(r.w[n * 4 + m] == L.compositions(n, m));
    CHECK(r.w[8 * 4 + 3] == 165);
}

TEST_CASE("compileLut refuses what the table backend cannot serve", "[lut]") {
    auto expr = compileLut(*parseDsl("B2/S/C25").ir);
    REQUIRE(std::holds_alternative<CompileError>(expr));
    CHECK(std::get<CompileError>(expr).message.find("no table") != std::string::npos);

    auto ir = *parseDsl("B3/S23").ir;
    std::get<Table>(ir.transition).entries.pop_back();
    auto bad = compileLut(ir);
    REQUIRE(std::holds_alternative<CompileError>(bad));
    CHECK(std::get<CompileError>(bad).message.find("invalid IR") != std::string::npos);
}
